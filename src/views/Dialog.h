#ifndef DIALOG_H__
#define DIALOG_H__

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "Constants.h"
#include "../platform.h"

namespace miata::views {

    struct Size2D {
        float width = 0;
        float height = 0;
    };

    // ダイアログの背景パネルと、方向キーでフォーカス移動できるコントロール群を管理する。
    // NSView操作の実体はDialog.mmに閉じ込め、ヘッダはAppKit型に依存しない。
    class DialogPanel {
    public:
        DialogPanel();
        ~DialogPanel();

        void Show(const std::string& title, const std::string& message, Size2D ideal_size);
        void Hide();
        // ボタン行の右寄せ配置とパネル全体のサイズ確定、画面中央への表示を行う。
        // コントロールを一通り追加し終えた後、IDialog::Open()の最後に一度だけ呼ぶ。
        void Layout();

        // 戻り値はコールバックで識別するためのID
        int AddButton(const std::string& label, bool is_default);
        int AddCheckbox(const std::string& label, bool initial);
        int AddPopup(const std::string& label, const std::vector<std::string>& options, int initial);
        // テキストフィールドはフォーカス可能リストに入れない(IME/標準入力を優先させるため)
        void AddTextField(const std::string& initial);
        // 直前にAddTextFieldで作ったフィールドにdelegateを設定する
        // (id<NSTextFieldDelegate>を(__bridge void*)で渡す。ヘッダをAppKit非依存に保つため)。
        void SetTextFieldDelegate(void* delegate);
        // 任意のNSView(void*で受け渡す)を積み上げレイアウトに埋め込む。
        // AddButton等の定型コントロールに当てはまらない用途向けの最小限のエスケープハッチ。
        // フォーカス可能リストには入れない(埋め込んだビュー自身がキー入力を処理する想定)。
        void AddCustomView(void* native_view, float height);

        bool GetCheckbox(int id) const;
        int GetPopupSelection(int id) const;
        std::string GetTextFieldValue() const;

        void NavigateUp();
        void NavigateDown();
        void NavigateLeft();
        void NavigateRight();
        void NavigateOk(); // フォーカス中コントロールのアクションを実行(ボタン押下/チェックボックストグル)

        // ボタン押下時に呼ばれる(idはAddButtonの戻り値)
        std::function<void(int)> on_button_;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

    class IDialog {
    public:
        IDialog(const char* id, std::function<void(IDialog&)> on_close);
        virtual ~IDialog();
        void Open();
        virtual void Navigate(constants::Navigate dir);
        virtual Size2D GetIdealSize() const { return Size2D{400, 0}; }
        inline const std::string& Id() { return id_; }
        inline bool IsOpened() const { return is_opened_; }
    protected:
        virtual void OnOpen() = 0; // panel_ にコントロールを組み立てる
        virtual void OnClose() {}
        virtual void OnButton(int button_id) {}
        virtual void OnCancel() { CloseDialog(); }
        void CloseDialog();
        DialogPanel panel_;
    private:
        std::string id_;
        std::function<void(IDialog&)> on_close_;
        bool is_opened_ = false;
    };

    class ConfirmDialog : public IDialog {
    public:
        struct arguments {
            std::string message_;
            std::string button_text_;
        };
        ConfirmDialog(std::function<void(IDialog&)> on_close, const arguments& args);
    protected:
        void OnOpen() override;
        void OnButton(int button_id) override;
    private:
        arguments args_;
        int ok_id_ = -1;
    };

    class YesNoDialog : public IDialog {
    public:
        struct arguments {
            std::string message_ = "";
            bool default_select_ = false;
            std::string yes_text_ = "YES";
            std::string no_text_ = "NO";
        };
        YesNoDialog(std::function<void(IDialog&)> on_close, arguments args);
        Size2D GetIdealSize() const override { return Size2D{400, 0}; }
        bool Result() const { return result_; }
    protected:
        void OnOpen() override;
        void OnButton(int button_id) override;
        void OnCancel() override;
    private:
        arguments args_;
        bool result_ = false;
        int yes_id_ = -1;
        int no_id_ = -1;
    };

    class InputTextDialog : public IDialog {
    public:
        struct arguments {
            std::string message_;
            std::string initial_text_;
        };
        InputTextDialog(std::function<void(IDialog&)> on_close, const arguments& args);
        // キャンセル時は nullopt
        const std::optional<std::string>& Result() const { return result_; }
    protected:
        void OnOpen() override;
        void OnButton(int button_id) override;
        void OnCancel() override;
    private:
        arguments args_;
        std::optional<std::string> result_;
        int ok_id_ = -1;
    };

    class CustomDialog : public IDialog {
    public:
        CustomDialog(std::function<void(IDialog&)> on_close, const CustomDialogSpec& spec);
        // キャンセル時は nullopt
        const std::optional<CustomDialogResult>& Result() const { return result_; }
    protected:
        void OnOpen() override;
        void OnButton(int button_id) override;
    private:
        CustomDialogSpec spec_;
        std::optional<CustomDialogResult> result_;
        std::vector<int> checkbox_ids_;
        std::vector<int> select_ids_;
        std::vector<int> button_ids_;
    };
}


#endif // DIALOG_H__

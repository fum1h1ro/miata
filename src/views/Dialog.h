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
        // ボタン行の右寄せ配置とパネル全体のサイズ確定、画面中央への表示、最初のカーソルの位置(SetInitialFocusの項目。
        // 無ければ先頭)の決定を行う。コントロールを一通り追加し終えた後、IDialog::Open()の最後に一度だけ呼ぶ。
        void Layout();

        // 戻り値はコールバックで識別するためのID。
        // ボタン・チェックボックス・選択リストの行のlabelは、`&x` の記法でショートカットを指定できる(ParseMnemonicLabel参照。
        // 画面では `&` が取り除かれ、xに下線が付く)。同じ文字を、先に追加した項目が持っていれば、後の項目は持たない(下線も付かない)。
        int AddButton(const std::string& label, bool is_default);
        int AddCheckbox(const std::string& label, bool initial);
        // 縦に並んだ選択肢の行(optionsの順。行間なし)を、細い箱で囲んで追加する。戻り値は行ごとのID(AddButtonと同じ番号空間)。
        // 各行はフォーカス可能で、カーソルを置いてNavigateOkするか、クリックすると、その行のIDで on_button_ が呼ばれる。
        // 行どうしと、チェックボックス・ボタンの間のカーソル移動は、他のコントロールと同じ(幾何学的な最近傍)。
        std::vector<int> AddSelectList(const std::vector<std::string>& options);
        // Layout() で最初にカーソルを置く項目(AddButton/AddCheckbox/AddSelectListの戻り値)。呼ばない、または見つからなければ先頭。
        void SetInitialFocus(int id);
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
        std::string GetTextFieldValue() const;

        void NavigateUp();
        void NavigateDown();
        void NavigateLeft();
        void NavigateRight();
        void NavigateOk(); // フォーカス中コントロールのアクションを実行(ボタン押下/チェックボックストグル/選択リストの行の確定)
        // ラベルの `&x` でkey(小文字のASCII英数字)をショートカットにした項目へ、カーソルを移して、NavigateOkと同じ作用をさせる。
        // そのような項目が無ければ(keyが0のときも)何もせずfalse。キーバインドとの優先順位は、呼ぶ側が決める。
        bool ActivateMnemonic(char key);

        // ボタン、または選択リストの行が確定されたときに呼ばれる(idはAddButton/AddSelectListの戻り値)。
        // 呼び出し元(NavigateOkやクリック)のコールスタックを抜けた、次のランループで呼ばれる。
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
        // ラベルの `&x` で指定された項目を、keyのキーで選ぶ(DialogPanel::ActivateMnemonic)。閉じた後は、項目が無いのでfalse
        bool ActivateMnemonic(char key) { return panel_.ActivateMnemonic(key); }
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
        // キャンセル(Esc)時は nullopt。ボタンで閉じたら button_index、選択リストの行で閉じたら select_index のどちらか一方だけが入る
        const std::optional<CustomDialogResult>& Result() const { return result_; }
    protected:
        void OnOpen() override;
        void OnButton(int button_id) override; // ボタンと、選択リストの行の両方が、ここに届く
    private:
        CustomDialogSpec spec_;
        std::optional<CustomDialogResult> result_;
        std::vector<int> checkbox_ids_;
        std::vector<int> select_ids_; // 選択リストの行のID(行の順)
        std::vector<int> button_ids_;
    };
}


#endif // DIALOG_H__

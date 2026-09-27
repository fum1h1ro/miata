#ifndef VIEWS_FILTER_LIST_DIALOG_H__
#define VIEWS_FILTER_LIST_DIALOG_H__

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "Dialog.h"

namespace miata { class FzfFilter; }

namespace miata::views {
    // 大量の文字列からfzf絞り込みで1件選ぶダイアログ。
    // フィルタ入力欄が常時フォーカスを持つため、上下矢印/Enter/Escapeは通常の
    // KeyBindingMap::Dialog経由の経路(IDialog::Navigate)ではなく、テキストフィールドの
    // delegate(FilterListDialog.mm内)からHandleXxx()へ直接配送される
    // (テキストフィールドがfirstResponderの間はMiataRootView::keyDown:自体が呼ばれないため)。
    class FilterListDialog : public IDialog {
    public:
        struct arguments {
            std::string title_;
            std::string message_;
            std::vector<std::string> items_;
        };

        FilterListDialog(std::function<void(IDialog&)> on_close, arguments args);
        ~FilterListDialog() override;

        Size2D GetIdealSize() const override { return Size2D{560, 0}; }
        // キャンセル時はnullopt
        const std::optional<std::string>& Result() const { return result_; }

        // AppKit側のヘルパークラス(FilterListDialog.mm)から呼ばれるコールバック群。
        // publicだが外部(Lua/Application.cc)から呼ぶAPIではない。
        void HandleQueryChanged(const std::string& query);
        void HandleMoveSelection(int delta);
        void HandleConfirm();
        void HandleCancel();
        // _MiataFilterListNSView::drawRect: から呼ばれる。AppKit型を持ち込まないためfloatで受ける。
        void DrawList(float visible_min_y, float visible_max_y);

    protected:
        void OnOpen() override;
        void OnClose() override;

    private:
        void RebuildList(const std::string& query);
        void UpdateListLayout();

        arguments args_;
        std::optional<std::string> result_;
        std::unique_ptr<FzfFilter> filter_;
        std::vector<std::string> filtered_;
        int selected_index_ = -1;

        struct Impl; // NSTextFieldのdelegate/NSScrollView等はFilterListDialog.mmに閉じ込める
        std::unique_ptr<Impl> impl_;
    };
}

#endif // VIEWS_FILTER_LIST_DIALOG_H__

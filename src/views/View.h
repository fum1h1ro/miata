#ifndef VIEW_H__
#define VIEW_H__

#include <filesystem>
#include <memory>
#include <optional>
#include <queue>
#include "Dialog.h"
#include "BrowserView.h"
#include "../models/Model.h"

namespace miata::views {
    class View {
    public:
        View();
        ~View();

        void Navigate(constants::Navigate dir);

        void RequestDialog(std::shared_ptr<IDialog> dialog);
        bool IsAnyDialogOpened() const
        {
            return current_dialog_ != nullptr;
        }
        // タイマーから定期的に呼ぶ。閉じたダイアログを検出してキューの次を開く。
        void CheckDialogState();
        // タイマーから定期的に呼ぶ(CheckDialogStateの後)。ディレクトリ監視で古くなった一覧を
        // 自動リロードする。ダイアログ表示中は保留する(FileListView::UpdateAutoReload参照)。
        void UpdateAutoReload();

        void NavigateForBrowser(constants::Navigate dir);
        void ToggleFocus();
        void Mark();
        void Unmark();
        void ToggleMark();
        models::FileListModel& CurrentList();
        models::FileListModel& OtherList();
        models::FileEntryModel& CurrentEntry();
        FileListView& CurrentFileListView();
        // カーソル(フォーカス)のあるペインと、ペインを指定して取得するアクセサ
        constants::Pane CurrentPane() const;
        FileListView& GetFileListView(constants::Pane pane);
        models::FileListModel& GetList(constants::Pane pane);
        // listを表示しているペインを再スキャンして最新にする。カーソルとマークは維持される
        // (FileListView::Reload参照。cursor_toの意味も同じ)。ファイル操作の後始末用で、
        // 失敗(ディレクトリが読めない等)しても一覧が変わらないだけなので呼び出し側には返さない。
        void ReloadList(const models::FileListModel& list, std::optional<std::filesystem::path> cursor_to = std::nullopt);
        // listを表示しているペインのマークをすべて解除して、再描画する(FileListView::ClearMarks参照)。
        void ClearListMarks(const models::FileListModel& list);

        // Quick Lookのプレビューを、areaの範囲の一覧に被せて表示する/閉じる(BrowserView::ToggleQuickLook
        // 参照)。呼んだ後に表示中ならtrue。Navigate::Cancel(Escなど)でも閉じる。
        bool ToggleQuickLook(constants::QuickLookArea area);
        // タイマーから定期的に呼ぶ。プレビューを、カーソル下のファイルに追従させる。
        void UpdateQuickLook();

    private:
        void OpenNextDialogIfNeeded();
        // listを表示しているペイン(どちらでもなければnullptr)
        FileListView* FindFileListView(const models::FileListModel& list);

        std::unique_ptr<BrowserView> browser_;
        std::shared_ptr<IDialog> current_dialog_;
        std::queue<std::shared_ptr<IDialog>> dialog_requests_;
    };
}


#endif // VIEW_H__

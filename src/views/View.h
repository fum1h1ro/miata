#ifndef VIEW_H__
#define VIEW_H__

#include <memory>
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

    private:
        void OpenNextDialogIfNeeded();

        std::unique_ptr<BrowserView> browser_;
        std::shared_ptr<IDialog> current_dialog_;
        std::queue<std::shared_ptr<IDialog>> dialog_requests_;
    };
}


#endif // VIEW_H__

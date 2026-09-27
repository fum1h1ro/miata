#ifndef VIEWS_BROWSER_VIEW_H__
#define VIEWS_BROWSER_VIEW_H__

#include <memory>
#include "FileListView.h"

namespace miata::views {
    // 左右2ペインのファイルリストをNSSplitViewで並べる。
    class BrowserView {
    public:
        BrowserView();
        ~BrowserView();

        void* NativeView() const;

        inline bool IsLeft() const
        {
            return cursorIndex_ == 0;
        }
        inline bool IsRight() const
        {
            return cursorIndex_ == 1;
        }
        void FocusLeft();
        void FocusRight();
        void ToggleFocus();
        models::FileEntryModel& CurrentFileEntryModel();

        inline std::shared_ptr<FileListView>& GetCurrentFileListView()
        {
            return cursorIndex_ == 0 ? left_ : right_;
        }

    private:
        std::shared_ptr<FileListView> left_;
        std::shared_ptr<FileListView> right_;
        int cursorIndex_ = 0;

        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}

#endif // VIEWS_BROWSER_VIEW_H__

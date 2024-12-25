#ifndef VIEWS_BROWSER_VIEW_H__
#define VIEWS_BROWSER_VIEW_H__

#include <memory>
#include "../widgets/Widget.h"
#include "FileListView.h"

namespace miata::views {
    class BrowserView : public widgets::HorizontalLayouter {
    public:
        BrowserView(const char* name, int w, int h);

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
        int cursorIndex_;
    };
}

#endif // VIEWS_BROWSER_VIEW_H__

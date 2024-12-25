#include "BrowserView.h"
#include "../models/Model.h"


namespace miata::views {
    BrowserView::BrowserView(const char* name, int w, int h) : HorizontalLayouter(name, w, h)
    {
        cursorIndex_ = 0;
        //split_ratio_ = 0.5f;
        auto& model = models::BrowserModel::Instance();
        left_ = std::make_shared<FileListView>("left", 0, 0, model.Left());
        right_ = std::make_shared<FileListView>("right", 0, 0, model.Right());

        AddChild(left_);
        AddChild(right_);
    }

    void BrowserView::FocusLeft()
    {
        cursorIndex_ = 0;
        left_->SetFocus(true);
        right_->SetFocus(false);
    }

    void BrowserView::FocusRight()
    {
        cursorIndex_ = 1;
        left_->SetFocus(false);
        right_->SetFocus(true);
    }

    void BrowserView::ToggleFocus()
    {
        if (IsLeft()) {
            FocusRight();
        }
        else {
            FocusLeft();
        }
    }
    models::FileEntryModel& BrowserView::CurrentFileEntryModel()
    {
        return GetCurrentFileListView()->GetCurrent().Model();
    }

    //void Browser::OnGuiImpl(bool window_resized)
    //{
    //    LeftToRightLayouter::OnGuiImpl(window_resized);
    //}
}

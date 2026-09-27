#import <AppKit/AppKit.h>
#include "BrowserView.h"
#include "../models/Model.h"

namespace miata::views {

struct BrowserView::Impl {
    NSSplitView* split_view = nil;
};

BrowserView::BrowserView() : impl_(std::make_unique<Impl>())
{
    auto& model = models::BrowserModel::Instance();
    left_ = std::make_shared<FileListView>(model.Left());
    right_ = std::make_shared<FileListView>(model.Right());

    impl_->split_view = [[NSSplitView alloc] initWithFrame:NSMakeRect(0, 0, 400, 400)];
    impl_->split_view.vertical = YES; // 左右分割
    impl_->split_view.dividerStyle = NSSplitViewDividerStyleThin;
    impl_->split_view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;

    [impl_->split_view addArrangedSubview:(__bridge NSView*)left_->NativeView()];
    [impl_->split_view addArrangedSubview:(__bridge NSView*)right_->NativeView()];

    FocusLeft();
}

BrowserView::~BrowserView() = default;

void* BrowserView::NativeView() const
{
    return (__bridge void*)impl_->split_view;
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

}

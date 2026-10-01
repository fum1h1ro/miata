#import <AppKit/AppKit.h>
#include "BrowserView.h"
#include "../models/Model.h"
#include "NSColorUtil.h"

namespace {
    // カーソル下のファイルが変わってから、プレビューを切り替えるまでの待ち。カーソルを動かし続けている間は
    // 切り替えず(読み込みが空振りするので)、止まったら切り替える。ティックは0.05秒周期なので実際は+0〜0.05秒。
    constexpr auto kQuickLookSettleDelay = std::chrono::milliseconds(100);
}

// 2ペイン(NSSplitView)と、その上に被せるプレビューの覆いを重ねて持つコンテナ。
// 覆いの位置はペインの位置から決める。左右の境界はドラッグで動くので、NSSplitViewがペインの
// サイズを変えるたび(ウィンドウのリサイズも含む)に合わせ直す。
@interface _MiataBrowserContainer : NSView <NSSplitViewDelegate>
@property (nonatomic, weak) NSSplitView* splitView;
@property (nonatomic, weak) NSView* overlay;
@property (nonatomic, assign) miata::views::constants::QuickLookArea overlayArea;
- (void)layoutOverlay;
@end
@implementation _MiataBrowserContainer
- (BOOL)isFlipped { return YES; }
- (void)layoutOverlay
{
    using miata::views::constants::QuickLookArea;
    NSArray<NSView*>* panes = self.splitView.arrangedSubviews;
    NSRect rect = self.splitView.frame;
    switch (self.overlayArea) {
    case QuickLookArea::Left:
        rect = [self convertRect:panes[0].frame fromView:self.splitView];
        break;
    case QuickLookArea::Right:
        rect = [self convertRect:panes[1].frame fromView:self.splitView];
        break;
    case QuickLookArea::Both:
        break;
    }
    self.overlay.frame = rect;
}
- (void)splitViewDidResizeSubviews:(NSNotification*)notification
{
    [self layoutOverlay];
}
@end

namespace miata::views {

struct BrowserView::Impl {
    _MiataBrowserContainer* container = nil;
    NSSplitView* split_view = nil;
};

BrowserView::BrowserView() : impl_(std::make_unique<Impl>())
{
    auto& model = models::BrowserModel::Instance();
    left_ = std::make_shared<FileListView>(model.Left());
    right_ = std::make_shared<FileListView>(model.Right());
    quick_look_ = std::make_unique<QuickLookView>();

    impl_->split_view = [[NSSplitView alloc] initWithFrame:NSMakeRect(0, 0, 400, 400)];
    impl_->split_view.vertical = YES; // 左右分割
    impl_->split_view.dividerStyle = NSSplitViewDividerStyleThin;
    impl_->split_view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;

    [impl_->split_view addArrangedSubview:(__bridge NSView*)left_->NativeView()];
    [impl_->split_view addArrangedSubview:(__bridge NSView*)right_->NativeView()];

    // 分割ビューの上にプレビューの覆いを重ねる(覆いは、表示するまで隠れている)
    NSView* overlay = (__bridge NSView*)quick_look_->NativeView();
    impl_->container = [[_MiataBrowserContainer alloc] initWithFrame:impl_->split_view.frame];
    // ペインの境目やスクロールバー、プレビューの見た目を、背景の明るさに合わせる(NSColorUtil.h参照)。
    // 子のビューすべてに効く。ダイアログはこのビューの外(contentViewの直下)なので、OSのテーマのまま
    impl_->container.appearance = AppearanceForBackground();
    [impl_->container addSubview:impl_->split_view];
    [impl_->container addSubview:overlay];
    impl_->container.splitView = impl_->split_view;
    impl_->container.overlay = overlay;
    impl_->split_view.delegate = impl_->container;

    FocusLeft();
}

BrowserView::~BrowserView() = default;

void* BrowserView::NativeView() const
{
    return (__bridge void*)impl_->container;
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

bool BrowserView::ToggleQuickLook(constants::QuickLookArea area)
{
    if (quick_look_area_ == area) {
        HideQuickLook();
        return false;
    }

    auto was_shown = quick_look_area_.has_value();
    quick_look_area_ = area;
    impl_->container.overlayArea = area;
    [impl_->container layoutOverlay];
    if (!was_shown) {
        // 出した時点のファイルは、待たずにすぐ表示する
        quick_look_->Show();
        quick_look_shown_ = quick_look_target_ = GetCurrentFileListView()->CurrentPath();
        quick_look_->SetFile(quick_look_shown_);
    }
    return true;
}

void BrowserView::HideQuickLook()
{
    quick_look_area_.reset();
    quick_look_->Hide();
    quick_look_shown_.reset();
    quick_look_target_.reset();
}

void BrowserView::UpdateQuickLook()
{
    if (!quick_look_area_) return;

    // カーソル下のファイルは、カーソル移動・ペインの切り替え・ディレクトリの移動・再読み込み・ソートなど
    // さまざまな経路で変わるので、通知を集めずに、毎ティック今のファイルを見て判断する
    auto now = std::chrono::steady_clock::now();
    auto current = GetCurrentFileListView()->CurrentPath();
    if (current != quick_look_target_) {
        quick_look_target_ = std::move(current);
        quick_look_target_since_ = now;
        return;
    }
    if (quick_look_target_ == quick_look_shown_) return;
    if (now - quick_look_target_since_ < kQuickLookSettleDelay) return;

    quick_look_shown_ = quick_look_target_;
    quick_look_->SetFile(quick_look_shown_);
}

}

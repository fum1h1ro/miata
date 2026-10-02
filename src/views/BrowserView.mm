#import <AppKit/AppKit.h>
#include "BrowserView.h"
#include "../models/Model.h"
#include "NSColorUtil.h"

namespace {
    // カーソル下のファイルが変わってから、プレビューを切り替えるまでの待ち。カーソルを動かし続けている間は
    // 切り替えず(読み込みが空振りするので)、止まったら切り替える。ティックは0.05秒周期なので実際は+0〜0.05秒。
    constexpr auto kQuickLookSettleDelay = std::chrono::milliseconds(100);
}

// 2ペイン(NSSplitView)と、その上に被せるプレビューの覆い、各ペインの下の検索バーを持つコンテナ。
// 覆いと検索バーの位置はペインの位置から決める。左右の境界はドラッグで動くので、NSSplitViewがペインの
// サイズを変えるたび(ウィンドウのリサイズも含む)に合わせ直す。
// 検索バーは、そのペインの下端に、ペインと同じ幅で重ねる。覆いより手前なので、覆っているときも見える
// (バーの分だけ一覧を縮めるのは、一覧の側。FileListView::SetBottomInset)。
@interface _MiataBrowserContainer : NSView <NSSplitViewDelegate>
@property (nonatomic, weak) NSSplitView* splitView;
@property (nonatomic, weak) NSView* overlay;
@property (nonatomic, assign) miata::views::constants::QuickLookArea overlayArea;
// 左右のペインの検索バー(添字はペイン番号)と、その高さ
@property (nonatomic, copy) NSArray<NSView*>* searchBars;
@property (nonatomic, assign) CGFloat searchBarHeight;
- (void)setSearchBar:(NSInteger)pane visible:(BOOL)visible;
- (void)layoutOverlay;
- (void)layoutSearchBars;
@end
@implementation _MiataBrowserContainer {
    BOOL _searchBarVisible[2];
}
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
// 各ペインの検索バーを、そのペインの下端に、ペインと同じ幅で置く。出さないバーは、高さ0にして隠す
- (void)layoutSearchBars
{
    NSArray<NSView*>* panes = self.splitView.arrangedSubviews;
    for (NSUInteger i = 0; i < self.searchBars.count && i < panes.count; ++i) {
        NSView* bar = self.searchBars[i];
        BOOL visible = _searchBarVisible[i];
        NSRect pane = [self convertRect:panes[i].frame fromView:self.splitView];
        CGFloat height = visible ? MIN(self.searchBarHeight, pane.size.height) : 0;
        NSRect frame = NSMakeRect(pane.origin.x, NSMaxY(pane) - height, pane.size.width, height);
        if (!NSEqualRects(bar.frame, frame)) bar.frame = frame;
        if (bar.hidden == visible) bar.hidden = !visible;
    }
}
- (void)setSearchBar:(NSInteger)pane visible:(BOOL)visible
{
    if (_searchBarVisible[pane] == visible) return;
    _searchBarVisible[pane] = visible;
    [self layoutSearchBars];
}
- (void)splitViewDidResizeSubviews:(NSNotification*)notification
{
    [self layoutOverlay];
    [self layoutSearchBars];
}
@end

namespace miata::views {

struct BrowserView::Impl {
    _MiataBrowserContainer* container = nil;
    NSSplitView* split_view = nil;
};

namespace {
    constexpr constants::Pane kPanes[] = {constants::Pane::Left, constants::Pane::Right};
}

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
    // 各ペインの下に検索バーを置く(そのペインに検索があるときだけ出す)。覆いより手前に重ねる
    NSMutableArray<NSView*>* search_bar_views = [NSMutableArray array];
    for (auto pane : kPanes) {
        search_bars_[static_cast<size_t>(pane)] = std::make_unique<SearchBar>();
        NSView* view = (__bridge NSView*)BarOf(pane).NativeView();
        view.hidden = YES;
        [search_bar_views addObject:view];
    }
    impl_->container = [[_MiataBrowserContainer alloc] initWithFrame:impl_->split_view.frame];
    // ペインの境目やスクロールバー、プレビューの見た目を、背景の明るさに合わせる(NSColorUtil.h参照)。
    // 子のビューすべてに効く。ダイアログはこのビューの外(contentViewの直下)なので、OSのテーマのまま
    impl_->container.appearance = AppearanceForBackground();
    [impl_->container addSubview:impl_->split_view];
    [impl_->container addSubview:overlay];
    for (NSView* view in search_bar_views) [impl_->container addSubview:view];
    impl_->container.splitView = impl_->split_view;
    impl_->container.overlay = overlay;
    impl_->container.searchBars = search_bar_views;
    impl_->container.searchBarHeight = (CGFloat)SearchBar::Height();
    impl_->split_view.delegate = impl_->container;

    // 検索バーの入力欄からの通知。Enter / Escは、入力欄が次のランループへ逃がしてから呼ぶ(SearchBar.h参照)ので、
    // 呼ばれたときには状態が変わっていることがある(ダイアログが開く直前に確定された等)。そのとき入力中の
    // ペインが、通知したバーのペインでなければ、何もしない
    for (auto pane : kPanes) {
        SearchBar::Callbacks callbacks;
        callbacks.on_query_changed = [this, pane] {
            PullSearchQuery(pane);
            SyncSearchBarView();
        };
        callbacks.on_step = [this, pane](int dir) {
            if (TypingPane() == pane) ViewOf(pane)->StepSearch(dir);
            SyncSearchBarView();
        };
        callbacks.on_commit = [this] {
            CommitSearchInput();
        };
        callbacks.on_cancel = [this, pane] {
            if (TypingPane() == pane) ViewOf(pane)->CancelSearch();
            UpdateSearchBar();
        };
        BarOf(pane).SetCallbacks(std::move(callbacks));
    }

    FocusLeft();
}

BrowserView::~BrowserView() = default;

void* BrowserView::NativeView() const
{
    return (__bridge void*)impl_->container;
}

void BrowserView::FocusLeft()
{
    CommitSearchInput(); // 入力中の検索は、確定してから切り替える
    cursorIndex_ = 0;
    left_->SetFocus(true);
    right_->SetFocus(false);
}

void BrowserView::FocusRight()
{
    CommitSearchInput();
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
models::FileEntryModel* BrowserView::CurrentFileEntryModel()
{
    auto* entry = GetCurrentFileListView()->CurrentOrNull();
    return entry ? &entry->Model() : nullptr;
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

std::optional<constants::Pane> BrowserView::TypingPane() const
{
    for (auto pane : kPanes) {
        if (ViewOf(pane)->GetSearchMode() == SearchMode::Typing) return pane;
    }
    return std::nullopt;
}

bool BrowserView::BeginSearch()
{
    if (TypingPane()) return false; // 入力中の「/」は、入力欄の文字になる(ここには来ない)

    auto pane = CurrentPane();
    auto& view = *ViewOf(pane);
    view.BeginSearch();
    // 入力欄をfirst responderにする前に、バーを出してレイアウトしておく(隠れたビューは文字を受けられない)。
    // UpdateSearchBar()は使わない: 入力欄がまだfirst responderでないので、「フォーカスを失った」と誤って確定してしまう
    SyncSearchBarView();
    if (!BarOf(pane).BeginInput()) {
        // 入力欄に文字を打てなかった。検索を始めなかったことにする(確定済みの検索があれば、それに戻る)
        view.CancelSearch();
        UpdateSearchBar();
        return false;
    }
    return true;
}

bool BrowserView::StepSearch(int dir)
{
    bool moved = GetCurrentFileListView()->StepSearch(dir);
    SyncSearchBarView();
    return moved;
}

bool BrowserView::ClearSearch()
{
    bool cleared = GetCurrentFileListView()->ClearSearch();
    UpdateSearchBar(); // 入力中だった場合は、入力欄を手放す
    return cleared;
}

void BrowserView::CommitSearchInput()
{
    if (auto typing = TypingPane()) {
        PullSearchQuery(*typing); // 入力欄の最新の文字を取り込んでから確定する(通知の取りこぼしに備える)
        ViewOf(*typing)->CommitSearch();
    }
    UpdateSearchBar(); // 入力中のペインが無くなったので、入力欄を手放す
}

void BrowserView::PullSearchQuery(constants::Pane pane)
{
    auto& view = *ViewOf(pane);
    if (view.GetSearchMode() != SearchMode::Typing) return;
    if (auto text = BarOf(pane).SettledText()) view.SetSearchQuery(*text);
}

void BrowserView::ReconcileSearchInput()
{
    for (auto pane : kPanes) {
        auto& view = *ViewOf(pane);
        auto& bar = BarOf(pane);
        if (view.GetSearchMode() == SearchMode::Typing) {
            if (!bar.IsEditing()) {
                // 入力欄がfirst responderを失った(CommitSearchInputの説明のとおり、ダイアログが閉じるときなど)。
                // 入力は終わったものとして確定する
                view.CommitSearch();
            }
            else {
                // 入力欄の文字と検索語を照らし合わせて、通知の取りこぼしに追いつく(同じなら何もしない)
                PullSearchQuery(pane);
            }
        }
        // 入力中でなければ、入力欄を手放して、編集できない状態に戻す(何度呼んでもよい)。入力中にディレクトリが
        // 移動して検索が消えたときなど、入力欄が残っているのは、ここで片付ける。バーを隠す前に行う
        // (隠れたビューがfirst responderのままだと、以降のキー入力がbeepになる)
        if (view.GetSearchMode() != SearchMode::Typing) bar.EndInput();
    }
}

void BrowserView::SyncSearchBarView()
{
    for (auto pane : kPanes) {
        auto& view = *ViewOf(pane);
        // そのペインに検索があるあいだだけ、そのペインの一覧を縮めて、空いた帯にバーを重ねる。反対側のペインには影響しない
        bool visible = view.GetSearchMode() != SearchMode::Idle;
        view.SetBottomInset(visible ? SearchBar::Height() : 0);
        [impl_->container setSearchBar:static_cast<NSInteger>(pane) visible:visible];
        BarOf(pane).Update(view.GetSearchStatus());
    }
}

void BrowserView::UpdateSearchBar()
{
    ReconcileSearchInput();
    SyncSearchBarView();
}

}

#import <AppKit/AppKit.h>
#include "BrowserView.h"
#include "../models/Model.h"
#include "NSColorUtil.h"

namespace {
    // カーソル下のファイルが変わってから、プレビューを切り替えるまでの待ち。カーソルを動かし続けている間は
    // 切り替えず(読み込みが空振りするので)、止まったら切り替える。ティックは0.05秒周期なので実際は+0〜0.05秒。
    constexpr auto kQuickLookSettleDelay = std::chrono::milliseconds(100);
}

// 2ペイン(NSSplitView)と、その上に被せるプレビューの覆い、各ペインの下の入力バーを持つコンテナ。
// 覆いと入力バーの位置はペインの位置から決める。左右の境界はドラッグで動くので、NSSplitViewがペインの
// サイズを変えるたび(ウィンドウのリサイズも含む)に合わせ直す。
// 入力バーは、そのペインの下端に、ペインと同じ幅で重ねる(種類が複数あれば、下端から種類の順に積む)。覆いより
// 手前なので、覆っているときも見える(バーの分だけ一覧を縮めるのは、一覧の側。FileListView::SetBottomInset)。
@interface _MiataBrowserContainer : NSView <NSSplitViewDelegate>
@property (nonatomic, weak) NSSplitView* splitView;
@property (nonatomic, weak) NSView* overlay;
@property (nonatomic, assign) miata::views::constants::QuickLookArea overlayArea;
// 入力バー(添字は ペイン番号 * 種類の数 + 種類)と、その高さ
@property (nonatomic, copy) NSArray<NSView*>* queryBars;
@property (nonatomic, assign) CGFloat queryBarHeight;
- (void)setQueryBar:(NSInteger)slot visible:(BOOL)visible;
- (void)layoutOverlay;
- (void)layoutQueryBars;
@end
@implementation _MiataBrowserContainer {
    BOOL _queryBarVisible[2 * miata::views::constants::kQueryKindCount];
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
// 各ペインの入力バーを、そのペインの下端に、ペインと同じ幅で置く。種類の順に、下端から積む。出さないバーは、高さ0にして隠す
- (void)layoutQueryBars
{
    using miata::views::constants::kQueryKindCount;
    NSArray<NSView*>* panes = self.splitView.arrangedSubviews;
    for (NSUInteger i = 0; i < panes.count && (i + 1) * kQueryKindCount <= self.queryBars.count; ++i) {
        NSRect pane = [self convertRect:panes[i].frame fromView:self.splitView];
        CGFloat bottom = NSMaxY(pane);
        CGFloat remaining = pane.size.height;
        for (NSUInteger k = 0; k < kQueryKindCount; ++k) {
            NSUInteger slot = i * kQueryKindCount + k;
            NSView* bar = self.queryBars[slot];
            BOOL visible = _queryBarVisible[slot];
            CGFloat height = visible ? MIN(self.queryBarHeight, remaining) : 0;
            NSRect frame = NSMakeRect(pane.origin.x, bottom - height, pane.size.width, height);
            if (!NSEqualRects(bar.frame, frame)) bar.frame = frame;
            if (bar.hidden == visible) bar.hidden = !visible;
            bottom -= height;
            remaining -= height;
        }
    }
}
- (void)setQueryBar:(NSInteger)slot visible:(BOOL)visible
{
    if (_queryBarVisible[slot] == visible) return;
    _queryBarVisible[slot] = visible;
    [self layoutQueryBars];
}
- (void)splitViewDidResizeSubviews:(NSNotification*)notification
{
    [self layoutOverlay];
    [self layoutQueryBars];
}
@end

namespace miata::views {

struct BrowserView::Impl {
    _MiataBrowserContainer* container = nil;
    NSSplitView* split_view = nil;
};

namespace {
    constexpr constants::Pane kPanes[] = {constants::Pane::Left, constants::Pane::Right};

    // 入力の種類ごとの違い(FileListViewのどの操作を呼ぶか、バーのプロンプトと中身)は、ここに集める。
    // 種類を足したら、これらのswitchにcaseを足す(足し忘れはコンパイラが警告する)。
    // 絞り込みのプロンプトは、一致のしかたで変わる(バーを作るときは、部分一致のもの。出すときは、DisplayOfが毎回渡す)
    const char* FilterPromptOf(MatchKind kind)
    {
        switch (kind) {
        case MatchKind::Substring: return "絞り込み";
        case MatchKind::Fuzzy: return "あいまい";
        }
        return "";
    }
    const char* PromptOf(constants::QueryKind kind)
    {
        switch (kind) {
        case constants::QueryKind::Search: return "/";
        case constants::QueryKind::Filter: return FilterPromptOf(MatchKind::Substring);
        }
        return "";
    }
    QueryMode ModeOf(FileListView& view, constants::QueryKind kind)
    {
        switch (kind) {
        case constants::QueryKind::Search: return view.GetSearchMode();
        case constants::QueryKind::Filter: return view.GetFilterMode();
        }
        return QueryMode::Idle;
    }
    // matchは、絞り込みの語の一致のしかた(検索では使わない)
    void BeginOf(FileListView& view, constants::QueryKind kind, MatchKind match)
    {
        switch (kind) {
        case constants::QueryKind::Search: view.BeginSearch(); break;
        case constants::QueryKind::Filter: view.BeginFilter(match); break;
        }
    }
    void SetQueryOf(FileListView& view, constants::QueryKind kind, const std::string& query)
    {
        switch (kind) {
        case constants::QueryKind::Search: view.SetSearchQuery(query); break;
        case constants::QueryKind::Filter: view.SetFilterQuery(query); break;
        }
    }
    void StepOf(FileListView& view, constants::QueryKind kind, int dir)
    {
        switch (kind) {
        case constants::QueryKind::Search: view.StepSearch(dir); break;
        case constants::QueryKind::Filter: view.StepFilter(dir); break;
        }
    }
    // explicit_enterは、ユーザーがEnterで確定した(true)か、成り行きで終わる(false)か。絞り込みの語が空のとき、
    // Enterなら解除(空で確定=解除)、成り行きなら取り消し(前の絞り込みに戻る)。語があれば、どちらも確定する
    void CommitOf(FileListView& view, constants::QueryKind kind, bool explicit_enter)
    {
        switch (kind) {
        case constants::QueryKind::Search: view.CommitSearch(); break;
        case constants::QueryKind::Filter:
            if (explicit_enter || !view.FilterQuery().empty()) view.CommitFilter();
            else view.CancelFilter();
            break;
        }
    }
    void CancelOf(FileListView& view, constants::QueryKind kind)
    {
        switch (kind) {
        case constants::QueryKind::Search: view.CancelSearch(); break;
        case constants::QueryKind::Filter: view.CancelFilter(); break;
        }
    }
    QueryBar::Display DisplayOf(FileListView& view, constants::QueryKind kind)
    {
        switch (kind) {
        case constants::QueryKind::Search:
            {
                auto status = view.GetSearchStatus();
                return QueryBar::Display{.active = status.mode != QueryMode::Idle, .query = status.query, .count = SearchCountText(status)};
            }
        case constants::QueryKind::Filter:
            {
                auto status = view.GetFilterStatus();
                return QueryBar::Display{.active = status.mode != QueryMode::Idle, .query = status.query, .count = FilterCountText(status),
                                         .prompt = FilterPromptOf(status.kind)};
            }
        }
        return {};
    }
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
    // 各ペインの下に入力バーを置く(そのペインにその種類の入力があるときだけ出す)。覆いより手前に重ねる
    NSMutableArray<NSView*>* bar_views = [NSMutableArray array];
    for (auto pane : kPanes) {
        for (auto kind : constants::kQueryKinds) {
            QueryTarget target{pane, kind};
            bars_[SlotIndex(target)] = std::make_unique<QueryBar>(PromptOf(kind));
            NSView* view = (__bridge NSView*)BarOf(target).NativeView();
            view.hidden = YES;
            [bar_views addObject:view];
        }
    }
    impl_->container = [[_MiataBrowserContainer alloc] initWithFrame:impl_->split_view.frame];
    // ペインの境目やスクロールバー、プレビューの見た目を、背景の明るさに合わせる(NSColorUtil.h参照)。
    // 子のビューすべてに効く。ダイアログはこのビューの外(contentViewの直下)なので、OSのテーマのまま
    impl_->container.appearance = AppearanceForBackground();
    [impl_->container addSubview:impl_->split_view];
    [impl_->container addSubview:overlay];
    for (NSView* view in bar_views) [impl_->container addSubview:view];
    impl_->container.splitView = impl_->split_view;
    impl_->container.overlay = overlay;
    impl_->container.queryBars = bar_views;
    impl_->container.queryBarHeight = (CGFloat)QueryBar::Height();
    impl_->split_view.delegate = impl_->container;

    // 入力欄からの通知。Enter / Escは、入力欄が次のランループへ逃がしてから呼ぶ(QueryBar.h参照)ので、呼ばれたときには
    // 状態が変わっていることがある(ダイアログが開く直前に終わらされた等)。そのとき入力中の対象が、通知したバーの
    // ものでなければ、何もしない
    for (auto pane : kPanes) {
        for (auto kind : constants::kQueryKinds) {
            QueryTarget target{pane, kind};
            QueryBar::Callbacks callbacks;
            callbacks.on_query_changed = [this, target] {
                PullQuery(target);
                SyncQueryBars();
            };
            callbacks.on_step = [this, target](int dir) {
                if (TypingTarget() == target) StepOf(*ViewOf(target.pane), target.kind, dir);
                SyncQueryBars();
            };
            callbacks.on_commit = [this, target] {
                if (TypingTarget() == target) FinishTyping(target, true);
                UpdateQueryBars();
            };
            callbacks.on_cancel = [this, target] {
                if (TypingTarget() == target) CancelOf(*ViewOf(target.pane), target.kind);
                UpdateQueryBars();
            };
            BarOf(target).SetCallbacks(std::move(callbacks));
        }
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
    SettleQueryInput(); // 入力中なら、終わらせてから切り替える
    cursorIndex_ = 0;
    left_->SetFocus(true);
    right_->SetFocus(false);
}

void BrowserView::FocusRight()
{
    SettleQueryInput();
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

std::optional<BrowserView::QueryTarget> BrowserView::TypingTarget() const
{
    for (auto pane : kPanes) {
        for (auto kind : constants::kQueryKinds) {
            if (ModeOf(*ViewOf(pane), kind) == QueryMode::Typing) return QueryTarget{pane, kind};
        }
    }
    return std::nullopt;
}

bool BrowserView::BeginInput(constants::QueryKind kind, MatchKind match)
{
    if (TypingTarget()) return false; // 入力中のキーは、入力欄の文字になる(ここには来ない)

    QueryTarget target{CurrentPane(), kind};
    auto& view = *ViewOf(target.pane);
    BeginOf(view, kind, match);
    // 入力欄をfirst responderにする前に、バーを出してレイアウトしておく(隠れたビューは文字を受けられない)。
    // UpdateQueryBars()は使わない: 入力欄がまだfirst responderでないので、「フォーカスを失った」と誤って確定してしまう
    SyncQueryBars();
    if (!BarOf(target).BeginInput()) {
        // 入力欄に文字を打てなかった。入力を始めなかったことにする(確定済みの語があれば、それに戻る)
        CancelOf(view, kind);
        UpdateQueryBars();
        return false;
    }
    return true;
}

bool BrowserView::BeginSearch()
{
    return BeginInput(constants::QueryKind::Search);
}

bool BrowserView::BeginFilter(MatchKind kind)
{
    return BeginInput(constants::QueryKind::Filter, kind);
}

bool BrowserView::ClearFilter(constants::Pane pane)
{
    bool cleared = ViewOf(pane)->ClearFilter();
    UpdateQueryBars(); // 入力中だった場合は、入力欄を手放す。バーも隠す
    return cleared;
}

FilterStatus BrowserView::SetFilter(constants::Pane pane, const std::string& query, MatchKind kind)
{
    SettleQueryInput(); // 入力中なら、先に終わらせる(入力欄が語を持ったままだと、設定した語を上書きしてしまう)
    auto status = ViewOf(pane)->SetFilter(query, kind);
    UpdateQueryBars();
    return status;
}

bool BrowserView::StepSearch(int dir)
{
    bool moved = GetCurrentFileListView()->StepSearch(dir);
    SyncQueryBars();
    return moved;
}

bool BrowserView::ClearSearch()
{
    bool cleared = GetCurrentFileListView()->ClearSearch();
    UpdateQueryBars(); // 入力中だった場合は、入力欄を手放す
    return cleared;
}

void BrowserView::FinishTyping(QueryTarget target, bool explicit_enter)
{
    PullQuery(target); // 入力欄の最新の文字を取り込んでから確定する(通知の取りこぼしに備える)
    CommitOf(*ViewOf(target.pane), target.kind, explicit_enter);
}

void BrowserView::SettleQueryInput()
{
    if (auto typing = TypingTarget()) FinishTyping(*typing, false);
    UpdateQueryBars(); // 入力中の対象が無くなったので、入力欄を手放す
}

void BrowserView::PullQuery(QueryTarget target)
{
    auto& view = *ViewOf(target.pane);
    if (ModeOf(view, target.kind) != QueryMode::Typing) return;
    if (auto text = BarOf(target).SettledText()) SetQueryOf(view, target.kind, *text);
}

void BrowserView::ReconcileInput()
{
    for (auto pane : kPanes) {
        for (auto kind : constants::kQueryKinds) {
            QueryTarget target{pane, kind};
            auto& view = *ViewOf(pane);
            auto& bar = BarOf(target);
            if (ModeOf(view, kind) == QueryMode::Typing) {
                if (!bar.IsEditing()) {
                    // 入力欄がfirst responderを失った(SettleQueryInputの説明のとおり、ダイアログが閉じるときなど)。
                    // 入力は終わったものとして、成り行きで終わらせる
                    FinishTyping(target, false);
                }
                else {
                    // 入力欄の文字と語を照らし合わせて、通知の取りこぼしに追いつく(同じなら何もしない)
                    PullQuery(target);
                }
            }
            // 入力中でなければ、入力欄を手放して、編集できない状態に戻す(何度呼んでもよい)。入力中にディレクトリが
            // 移動して検索や絞り込みが消えたときなど、入力欄が残っているのは、ここで片付ける。バーを隠す前に行う
            // (隠れたビューがfirst responderのままだと、以降のキー入力がbeepになる)
            if (ModeOf(view, kind) != QueryMode::Typing) bar.EndInput();
        }
    }
}

void BrowserView::SyncQueryBars()
{
    for (auto pane : kPanes) {
        auto& view = *ViewOf(pane);
        // そのペインに入力(入力中・確定済み)があるあいだだけ、そのペインの一覧を縮めて、空いた帯にバーを重ねる。
        // 反対側のペインには影響しない
        double inset = 0;
        for (auto kind : constants::kQueryKinds) {
            if (ModeOf(view, kind) != QueryMode::Idle) inset += QueryBar::Height();
        }
        view.SetBottomInset(inset);
        for (auto kind : constants::kQueryKinds) {
            QueryTarget target{pane, kind};
            [impl_->container setQueryBar:static_cast<NSInteger>(SlotIndex(target)) visible:ModeOf(view, kind) != QueryMode::Idle];
            BarOf(target).Update(DisplayOf(view, kind));
        }
    }
}

void BrowserView::UpdateQueryBars()
{
    ReconcileInput();
    SyncQueryBars();
}

}

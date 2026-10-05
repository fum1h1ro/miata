#import <AppKit/AppKit.h>
#include <algorithm>
#include <cmath>
#include <format>
#include <unordered_map>
#include "FileListView.h"
#include "../platform.h"
#include "../Config.h"
#include "NSColorUtil.h"
#include "ViewMetrics.h"

namespace {
    constexpr CGFloat kDragStartDistance = 4; // 押下位置からこれ以上動いたらドラッグ開始とみなす(pt)
    constexpr CGFloat kDragIconSize = 32;     // ドラッグ画像1件分の大きさ(pt)
    constexpr NSUInteger kMaxDragImages = 16; // ドラッグ画像(アイコン)を用意する最大件数
}

// ファイル一覧本体のビュー。キーボード操作が主体のアプリなので、マウスで受け付けるのは
// 「マーク済みファイルのドラッグ」だけ(クリックでのカーソル移動等は行わない)。
// first responderにはならないため、キー入力はこれまで通りMiataRootViewに届く。
@interface _MiataFileListNSView : NSView <NSDraggingSource>
@property (nonatomic, assign) miata::views::FileListView* owner;
@end
@implementation _MiataFileListNSView {
    NSPoint _mouseDownPoint;
    BOOL _dragPending; // mouseDown後、まだドラッグを開始(または見送り)していない
}
- (BOOL)isFlipped { return YES; }
// 非アクティブなウィンドウ上でも、最初の1回のマウス操作でそのままドラッグを始められるようにする
- (BOOL)acceptsFirstMouse:(NSEvent*)event { return YES; }
- (void)drawRect:(NSRect)dirtyRect
{
    if (self.owner) self.owner->Draw(NSMinY(dirtyRect), NSMaxY(dirtyRect));
}
- (void)mouseDown:(NSEvent*)event
{
    _mouseDownPoint = [self convertPoint:event.locationInWindow fromView:nil];
    _dragPending = YES;
}
- (void)mouseDragged:(NSEvent*)event
{
    if (!_dragPending || !self.owner) return;
    NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
    if (std::hypot(p.x - _mouseDownPoint.x, p.y - _mouseDownPoint.y) < kDragStartDistance) return;
    _dragPending = NO; // このmouseDownの間は、開始できなかった場合も含めて2度目以降を試さない

    // 何を運ぶかはFileListViewが決める(マーク済みのみ。押した行は問わない)
    const auto& entries = self.owner->BeginDrag();
    if (entries.empty()) return;

    NSMutableArray<NSDraggingItem*>* items = [NSMutableArray arrayWithCapacity:entries.size()];
    for (const auto& entry : entries) {
        // パスをNSStringに変換せず、バイト列から直接URLを作る(変換できない名前でも例外にならない)
        NSURL* url = [NSURL fileURLWithFileSystemRepresentation:entry.path.c_str()
                                                    isDirectory:entry.is_directory
                                                  relativeToURL:nil];
        if (!url) continue;

        // アイコンは先頭の一部だけ用意する(数千件マークしていても開始が重くならないように)。
        // 残りは画像なし(contents=nil)で、ペーストボードには載せて運ぶ。画像なしでも
        // draggingFrameにサイズ0は指定できない(NSRangeException)ので、枠は全件同じ大きさにする。
        NSImage* icon = items.count < kMaxDragImages ? [[NSWorkspace sharedWorkspace] iconForFile:url.path] : nil;
        NSDraggingItem* item = [[NSDraggingItem alloc] initWithPasteboardWriter:url];
        [item setDraggingFrame:NSMakeRect(_mouseDownPoint.x - kDragIconSize / 2, _mouseDownPoint.y - kDragIconSize / 2, kDragIconSize, kDragIconSize)
                      contents:icon];
        [items addObject:item];
    }

    NSDraggingSession* session = items.count > 0 ? [self beginDraggingSessionWithItems:items event:event source:self] : nil;
    if (!session) {
        self.owner->EndDrag(false);
        return;
    }
    session.draggingFormation = items.count > 1 ? NSDraggingFormationPile : NSDraggingFormationNone;
}

// NSDraggingSource
- (NSDragOperation)draggingSession:(NSDraggingSession*)session sourceOperationMaskForDraggingContext:(NSDraggingContext)context
{
    // コピーになるか移動になるかは、宛先アプリと修飾キー(Option=コピー等)で決まる。
    // 自アプリ内にはドロップ先が無いので、どの文脈でも同じ値を返す。
    return NSDragOperationCopy | NSDragOperationMove | NSDragOperationLink | NSDragOperationGeneric;
}
- (void)draggingSession:(NSDraggingSession*)session endedAtPoint:(NSPoint)screenPoint operation:(NSDragOperation)operation
{
    if (self.owner) self.owner->EndDrag(operation != NSDragOperationNone);
}
@end

// ヘッダー(パス表示)自体の背景ビュー。
@interface _MiataFileListHeaderView : NSView
@property (nonatomic, assign) miata::views::FileListView* owner;
@end
@implementation _MiataFileListHeaderView
- (BOOL)isFlipped { return YES; }
- (void)drawRect:(NSRect)dirtyRect
{
    if (self.owner) self.owner->DrawHeader();
}
@end

// ヘッダーとスクロール可能なリストを縦に並べるコンテナ。
// isFlipped=YESなビュー内ではautoresizingMaskのY軸マージン(NSViewMinYMargin/MaxYMargin)の
// 意味が反転する既知の癖があり、意図通りに追従しないため、フレーム変更時に
// 子ビューの矩形を明示的に再計算する。
@interface _MiataFileListLayoutContainer : NSView
@property (nonatomic, weak) NSView* headerView;
@property (nonatomic, weak) NSView* scrollView;
@property (nonatomic, assign) CGFloat headerHeight;
// スクロール部分の下に空ける高さ。検索バーを重ねる帯(FileListView::SetBottomInset)
@property (nonatomic, assign) CGFloat footerHeight;
- (void)layoutChildren;
@end
@implementation _MiataFileListLayoutContainer
- (BOOL)isFlipped { return YES; }
- (void)layoutChildren
{
    CGFloat w = self.bounds.size.width;
    CGFloat h = self.bounds.size.height;
    self.headerView.frame = NSMakeRect(0, 0, w, self.headerHeight);
    self.scrollView.frame = NSMakeRect(0, self.headerHeight, w, MAX((CGFloat)0, h - self.headerHeight - self.footerHeight));
}
- (void)setFooterHeight:(CGFloat)height
{
    if (_footerHeight == height) return;
    _footerHeight = height;
    [self layoutChildren];
}
- (void)setFrameSize:(NSSize)newSize
{
    [super setFrameSize:newSize];
    [self layoutChildren];
}
// NSSplitViewがフレームをどう設定してもここは確実に描画直前に呼ばれるため、
// setFrameSize:での追従が効かない場合の保険として毎回レイアウトし直す。
- (void)viewWillDraw
{
    [self layoutChildren];
    [super viewWillDraw];
}
@end

namespace miata::views {

namespace {
    constexpr CGFloat kPadding = kListPadding;
    constexpr CGFloat kRowVerticalMargin = 8; // 行の上下に確保する余白の合計

    // ファイル一覧の行の高さは、フォントサイズに応じて動的に決める
    // (固定値のままだとフォントサイズを上げた時に行同士が重なってしまうため)。
    // ヘッダーの高さとフォントはViewMetrics.h(検索バーと共有)。
    CGFloat RowHeight()
    {
        return Config::FontSize() + kRowVerticalMargin;
    }

    // ls -lh 風の簡易フォーマット。将来的に外部指定できるようにするまでの固定実装。
    std::string FormatSize(uintmax_t bytes)
    {
        static const char* units[] = {"B", "K", "M", "G", "T"};
        double size = (double)bytes;
        int unit = 0;
        while (size >= 1024.0 && unit < 4) {
            size /= 1024.0;
            ++unit;
        }
        if (unit == 0) {
            return std::format("{}{}", (uintmax_t)size, units[unit]);
        }
        return std::format("{:.1f}{}", size, units[unit]);
    }

    // ファイル名/拡張子のソート用。大文字小文字を区別しない(Unicodeの大文字小文字も正しく畳み込む)。
    NSComparisonResult CaseInsensitiveCompare(const std::string& a, const std::string& b)
    {
        return [@(a.c_str()) caseInsensitiveCompare:@(b.c_str())];
    }

    // ドロップ受理から、宛先アプリがファイルを動かし終えたかを確認するまでの待ち時間(秒)
    constexpr double kDropSettleSeconds = 0.3;

    // ディレクトリ監視による自動リロード
    constexpr auto kAutoReloadDelay = std::chrono::milliseconds(300);       // 変化を検知してから反映するまでの待ち(続く変化を1回にまとめる)
    constexpr auto kAutoReloadMinInterval = std::chrono::milliseconds(500); // 自動リロードどうしの最短の間隔
    constexpr int kAutoReloadCostFactor = 8;                                // 走査が重いときは、かかった時間のこの倍以上あける

    // 実体が確実に無くなっている場合のみtrue。エラーで判定できない場合や、壊れた
    // シンボリックリンク(リンク自体は残っている)はfalse。
    bool IsGone(const std::filesystem::path& path)
    {
        std::error_code ec;
        return std::filesystem::symlink_status(path, ec).type() == std::filesystem::file_type::not_found;
    }

    // name(UTF-16)の中の、needleの出現箇所をすべて、左から重ならないように見つける。範囲はnameの添字
    // (検索語の長さと同じとは限らない。合成済みと分解された文字は同じ文字として一致するため)なので、
    // そのまま属性文字列の範囲に使える。
    std::vector<SearchRange> FindAllOccurrences(NSString* name, NSString* needle, NSStringCompareOptions options)
    {
        std::vector<SearchRange> found;
        NSUInteger length = name.length;
        NSUInteger position = 0;
        while (position < length) {
            NSRange range = [name rangeOfString:needle options:options range:NSMakeRange(position, length - position)];
            if (range.location == NSNotFound || range.length == 0) break;
            found.push_back(SearchRange{range.location, range.length});
            position = NSMaxRange(range);
        }
        return found;
    }
}

struct FileListView::Impl {
    _MiataFileListLayoutContainer* container = nil;
    NSView* header_view = nil;
    NSScrollView* scroll_view = nil;
    _MiataFileListNSView* content_view = nil;
    id frame_observer = nil; // クリップビューの幅変化をdocumentViewに追従させるための監視トークン

    ~Impl()
    {
        if (frame_observer) {
            [[NSNotificationCenter defaultCenter] removeObserver:frame_observer];
        }
    }
};

FileListView::FileListView(models::FileListModel& list) : model_(list), impl_(std::make_unique<Impl>())
{
    CGFloat header_height = HeaderHeight();
    impl_->container = [[_MiataFileListLayoutContainer alloc] initWithFrame:NSMakeRect(0, 0, 200, 200)];
    impl_->container.headerHeight = header_height;

    _MiataFileListHeaderView* header = [[_MiataFileListHeaderView alloc] initWithFrame:NSMakeRect(0, 0, 200, header_height)];
    header.owner = this;
    impl_->header_view = header;
    [impl_->container addSubview:impl_->header_view];
    impl_->container.headerView = impl_->header_view;

    impl_->content_view = [[_MiataFileListNSView alloc] initWithFrame:NSMakeRect(0, 0, 200, 200 - header_height)];
    impl_->content_view.owner = this;

    impl_->scroll_view = [[NSScrollView alloc] initWithFrame:NSMakeRect(0, header_height, 200, 200 - header_height)];
    impl_->scroll_view.hasVerticalScroller = YES;
    impl_->scroll_view.hasHorizontalScroller = NO;
    impl_->scroll_view.drawsBackground = NO;
    impl_->scroll_view.documentView = impl_->content_view;
    [impl_->container addSubview:impl_->scroll_view];
    impl_->container.scrollView = impl_->scroll_view;
    [impl_->container layoutChildren];

    // NSSplitViewのドラッグやウィンドウリサイズでクリップビューの幅が変わるたびに
    // documentView(content_view)の幅を追従させる。autoresizingMaskだけでは
    // 初期化直後(ウィンドウ未配置)の不正確な幅が基準になってしまうため通知で確実に同期する。
    impl_->scroll_view.contentView.postsFrameChangedNotifications = YES;
    FileListView* self_ptr = this;
    impl_->frame_observer = [[NSNotificationCenter defaultCenter]
        addObserverForName:NSViewFrameDidChangeNotification
                    object:impl_->scroll_view.contentView
                     queue:nil
                usingBlock:^(NSNotification*) {
        self_ptr->Redraw();
    }];

    subscriptions_.push_back(
        model_.ObservePath()
            .subscribe([this](const std::filesystem::path& path) {
                // 再スキャン(Reload)ではない通知は、別のディレクトリへの移動。検索は消す(再スキャンでは、
                // 検索語を保ったまま、Fetch()でヒットを作り直す)
                if (!reload_memo_) search_.Clear();
                Fetch();
                // Reload()経由の通知ならカーソルを復元する。ディレクトリ移動(JumpTo)は先頭から
                cursorIndex_ = reload_memo_ ? RestoreCursor(*reload_memo_) : 0;
                Redraw();
                // 別のディレクトリへの移動なら、先頭から表示する。Redraw()がカーソルの行へスクロールするのはフォーカスの
                // あるペインだけなので、フォーカスの無いペインを移したとき(jump_to(path, pane))に、前のディレクトリの
                // スクロール位置が残らないようにする
                if (!reload_memo_) [impl_->content_view scrollPoint:NSZeroPoint];
                // 走査した内容が最新になったので、反映待ちは不要。監視は、表示するパスが変わったときだけ
                // 張り直す(張り直しの中で、走査より後の変更を見つけたら、また反映待ちになる)
                stale_ = false;
                WatchDirectory(path);
            })
    );

    Fetch();
}

FileListView::~FileListView() = default;

void* FileListView::NativeView() const
{
    return (__bridge void*)impl_->container;
}

void FileListView::SetSort(SortKey key, bool reverse)
{
    sort_key_ = key;
    sort_reverse_ = reverse;
    Fetch();
    Redraw();
}

std::optional<FileListView::SortKey> FileListView::ParseSortKey(std::string_view name)
{
    if (name == "name") return SortKey::Name;
    if (name == "size") return SortKey::Size;
    if (name == "mtime") return SortKey::ModifiedTime;
    if (name == "ext") return SortKey::Extension;
    return std::nullopt;
}

const char* FileListView::SortKeyName(SortKey key)
{
    switch (key) {
    case SortKey::Size: return "size";
    case SortKey::ModifiedTime: return "mtime";
    case SortKey::Extension: return "ext";
    case SortKey::Name:
    default: return "name";
    }
}

void FileListView::SetFocus(bool focus)
{
    focus_ = focus;
    Redraw();
}

void FileListView::SetCursor(int index)
{
    cursorIndex_ = index;
    Redraw();
}

void FileListView::MoveCursor(int offset)
{
    cursorIndex_ += offset;
    Redraw();
}

std::optional<std::filesystem::path> FileListView::CurrentPath() const
{
    if (cursorIndex_ < 0 || (size_t)cursorIndex_ >= list_.size()) return std::nullopt;
    return list_[(size_t)cursorIndex_]->Model().Path();
}

std::vector<models::FileEntryModel*> FileListView::MarkedEntries() const
{
    // list_は画面表示順(ソート済み)。貼り付け先でも、スクリプトでも、画面と同じ並びになるようこの順で集める
    std::vector<models::FileEntryModel*> marked;
    for (auto* entry : list_) {
        auto& entry_model = entry->Model();
        if (entry_model.IsMarked()) marked.push_back(&entry_model);
    }
    return marked;
}

void FileListView::Fetch()
{
    list_.clear();
    entries_.clear();
    auto size = model_.Size();
    list_.reserve((size_t)size);
    entries_.reserve((size_t)size);

    for (auto i = 0; i < size; i++) {
        auto& entry = model_.GetEntry(i);
        entries_.push_back(FileEntryView(entry));
        list_.push_back(&entries_[(size_t)i]);
    }

    std::sort(list_.begin(), list_.end(), [this](FileEntryView* a, FileEntryView* b) {
        if (a->Model().IsDirectory() != b->Model().IsDirectory()) return a->Model().IsDirectory();

        // reverse時は引数を入れ替えて同じ比較を行う(否定すると同値要素で狭義弱順序が壊れるため)
        auto* x = sort_reverse_ ? b : a;
        auto* y = sort_reverse_ ? a : b;
        switch (sort_key_) {
        case SortKey::Size:
            return x->Model().Size() < y->Model().Size();
        case SortKey::ModifiedTime:
            return x->Model().ModifiedTime() < y->Model().ModifiedTime();
        case SortKey::Extension:
            {
                auto cmp = CaseInsensitiveCompare(x->Model().Ext(), y->Model().Ext());
                if (cmp != NSOrderedSame) return cmp == NSOrderedAscending;
                return CaseInsensitiveCompare(x->Model().Name(), y->Model().Name()) == NSOrderedAscending;
            }
        case SortKey::Name:
        default:
            return CaseInsensitiveCompare(x->Model().Name(), y->Model().Name()) == NSOrderedAscending;
        }
    });

    // ヒットは表示順の添字なので、並べ直したら作り直す(検索していなければ何もしない)
    RebuildSearchHits();
}

void FileListView::ClearMarks()
{
    model_.ClearMarks();
    Redraw();
}

std::expected<void, FileError> FileListView::Reload(std::optional<std::filesystem::path> cursor_to)
{
    // 再スキャンで旧エントリが破棄される前に、カーソル復元に必要な情報を控える。
    // 復元自体は、再スキャン後のパス変更通知の購読側(コンストラクタ)がFetch()の直後に行う。
    CursorMemo memo;
    memo.order.reserve(list_.size());
    for (auto* entry : list_) {
        memo.order.push_back(entry->Model().Path());
    }
    memo.index = cursorIndex_;
    memo.target = std::move(cursor_to);

    reload_memo_ = std::move(memo);
    auto result = model_.Reload();
    reload_memo_.reset();
    return result;
}

int FileListView::RestoreCursor(const CursorMemo& memo) const
{
    if (list_.empty()) return 0;

    std::unordered_map<std::filesystem::path, int> index_of;
    index_of.reserve(list_.size());
    for (size_t i = 0; i < list_.size(); ++i) {
        index_of.emplace(list_[i]->Model().Path(), (int)i);
    }

    // 合わせる先を指定されていて一覧にあれば、そこへ
    if (memo.target) {
        if (auto it = index_of.find(*memo.target); it != index_of.end()) return it->second;
    }
    if (memo.order.empty()) return 0;

    // カーソルのファイルが残っていればそれを指す(並びが変わっても同じファイルに追従する)。
    // 消えていたら、再スキャン前の並びで「カーソル以降→カーソルより前」の順に最初に残っている
    // ファイルへ寄せる(Finderで選択中のファイルを消すと次の行へ移るのと同じ感覚)。
    auto size = (int)memo.order.size();
    auto start = std::clamp(memo.index, 0, size - 1);
    for (auto i = start; i < size; ++i) {
        if (auto it = index_of.find(memo.order[(size_t)i]); it != index_of.end()) return it->second;
    }
    for (auto i = start - 1; i >= 0; --i) {
        if (auto it = index_of.find(memo.order[(size_t)i]); it != index_of.end()) return it->second;
    }
    return 0;
}

void FileListView::Redraw()
{
    auto size = (int)list_.size();
    if (cursorIndex_ < 0) cursorIndex_ = 0;
    if (cursorIndex_ >= size) cursorIndex_ = std::max(0, size - 1);

    CGFloat row_height = RowHeight();
    NSRect content_frame = impl_->content_view.frame;
    CGFloat needed_height = std::max((CGFloat)size * row_height, impl_->scroll_view.contentSize.height);
    CGFloat needed_width = impl_->scroll_view.contentSize.width;
    if (content_frame.size.height != needed_height || content_frame.size.width != needed_width) {
        content_frame.size.height = needed_height;
        content_frame.size.width = needed_width;
        impl_->content_view.frame = content_frame;
    }

    if (focus_ && size > 0) {
        NSRect row_rect = NSMakeRect(0, (CGFloat)cursorIndex_ * row_height, content_frame.size.width, row_height);
        [impl_->content_view scrollRectToVisible:row_rect];
    }

    [impl_->header_view setNeedsDisplay:YES];
    [impl_->content_view setNeedsDisplay:YES];
}

void FileListView::RebuildSearchHits()
{
    std::vector<SearchHit> hits;
    // 検索していないとき(Idle)の語は、常に空(SearchState::Query)。不正なUTF-8だとneedleはnilで、長さは0
    NSString* needle = @(search_.Query().c_str());
    if (needle.length > 0) {
        // スマートケース: 検索語に大文字(Unicodeの大文字を含む)があれば大文字小文字を区別し、無ければ区別しない。
        // NSLiteralSearchは付けない(付けると、合成済みの文字と分解された文字(「が」と「か」+濁点)が一致しなくなる)。
        bool has_upper = [needle rangeOfCharacterFromSet:[NSCharacterSet uppercaseLetterCharacterSet]].location != NSNotFound;
        NSStringCompareOptions options = has_upper ? 0 : NSCaseInsensitiveSearch;
        for (size_t i = 0; i < list_.size(); ++i) {
            @autoreleasepool {
                NSString* name = @(list_[i]->Model().Name().c_str());
                if (!name) continue;
                auto ranges = FindAllOccurrences(name, needle, options);
                if (!ranges.empty()) hits.push_back(SearchHit{(int)i, std::move(ranges)});
            }
        }
    }
    search_.SetHits(std::move(hits));
}

int FileListView::ResolveRow(const SearchPosition& position) const
{
    if (list_.empty()) return -1;
    if (position.path) {
        for (size_t i = 0; i < list_.size(); ++i) {
            if (list_[i]->Model().Path() == *position.path) return (int)i;
        }
    }
    // パスが一覧に無い(消えた等)。代わりに、控えておいた添字の近くへ
    return std::clamp(position.row, 0, (int)list_.size() - 1);
}

SearchPosition FileListView::PositionAt(int row) const
{
    if (row < 0 || (size_t)row >= list_.size()) return {};
    return SearchPosition{list_[(size_t)row]->Model().Path(), row};
}

void FileListView::JumpCursorTo(int row)
{
    if (list_.empty()) {
        Redraw();
        return;
    }
    row = std::clamp(row, 0, (int)list_.size() - 1);

    CGFloat row_height = RowHeight();
    NSRect row_rect = NSMakeRect(0, (CGFloat)row * row_height, 1, row_height);
    bool was_visible = NSContainsRect(impl_->content_view.visibleRect, row_rect);

    cursorIndex_ = row;
    Redraw(); // 行が見える最小限のスクロールも、ここで行われる

    // 見えている範囲の外へ飛んだときは、行を画面の中央に出す(見えている範囲の中なら、画面は動かさない)
    if (!was_visible && focus_) {
        CGFloat visible_height = impl_->content_view.visibleRect.size.height;
        CGFloat document_height = impl_->content_view.frame.size.height;
        CGFloat y = (CGFloat)row * row_height + row_height / 2 - visible_height / 2;
        y = std::clamp(y, (CGFloat)0, std::max((CGFloat)0, document_height - visible_height));
        [impl_->content_view scrollPoint:NSMakePoint(0, y)];
    }
}

void FileListView::BeginSearch()
{
    if (search_.Mode() == SearchMode::Typing) return;

    search_.Begin(PositionAt(cursorIndex_)); // 入力するまでヒットは無い(前の検索のハイライトも隠れる)
    Redraw();
}

void FileListView::SetSearchQuery(const std::string& query)
{
    if (search_.Mode() != SearchMode::Typing || query == search_.Query()) return;

    search_.SetQuery(query);
    RebuildSearchHits();
    // 語を全部消したら、検索を始めた位置からやり直す(↓ / ↑で動かした起点も戻す)
    if (query.empty()) search_.SetAnchor(search_.Origin());

    // 起点から前方の最初のマッチ(起点自身を含む)へ。マッチが無ければ起点に戻る
    int base = ResolveRow(search_.Anchor());
    auto target = search_.FirstHitFrom(std::max(base, 0));
    JumpCursorTo(target.value_or(base));
}

bool FileListView::StepSearch(int dir)
{
    if (!search_.Active()) return false;

    auto row = search_.Step(cursorIndex_, dir);
    if (!row) return false;

    // 入力中に↓ / ↑で移ったときは、以降の入力を、移った先から探す
    if (search_.Mode() == SearchMode::Typing) search_.SetAnchor(PositionAt(*row));
    JumpCursorTo(*row);
    return true;
}

void FileListView::CommitSearch()
{
    if (search_.Mode() != SearchMode::Typing) return;

    // 語があれば、確定しても語もヒットも変わらない。語が空のときは、検索を始める前の状態に戻る
    // (確定済みの検索があれば、その語に戻る)ので、ヒットを作り直す
    bool had_query = !search_.Query().empty();
    search_.Commit();
    if (!had_query) RebuildSearchHits();
    Redraw();
}

void FileListView::CancelSearch()
{
    if (search_.Mode() != SearchMode::Typing) return;

    search_.Cancel(); // 戻り先(Origin)は消えない。消えるのはClear()のとき
    RebuildSearchHits();
    JumpCursorTo(ResolveRow(search_.Origin()));
}

bool FileListView::ClearSearch()
{
    if (!search_.Active()) return false;

    search_.Clear();
    Redraw();
    return true;
}

void FileListView::SetBottomInset(double height)
{
    if (impl_->container.footerHeight == (CGFloat)height) return;
    impl_->container.footerHeight = (CGFloat)height;
    // 一覧が縮んだので、カーソルが見える位置を保つ
    Redraw();
}

SearchStatus FileListView::GetSearchStatus() const
{
    SearchStatus status;
    status.mode = search_.Mode();
    status.query = search_.Query();
    status.hit_count = search_.HitCount();
    status.ordinal = search_.Ordinal(cursorIndex_);
    return status;
}

void FileListView::WatchDirectory(const std::filesystem::path& dir)
{
    // 同じパスを見ている監視が生きていれば、そのまま使う(再スキャンごとに張り直すと、その間の変更を落とす)
    if (watch_ && watched_dir_ == dir) return;

    watch_.reset(); // 古い監視を先に止める
    watched_dir_ = dir;
    watch_ = pl_watch_directory(dir, [this] { OnDirectoryChanged(); });
    if (!watch_) return; // 監視できないディレクトリ(権限が無い等)は、自動では更新しない(次の走査で張り直しを試す)

    // 走査してから監視を張るまでの間の変更は通知されないので、走査より後に変わっていないか確かめる
    // (ディレクトリの更新日時が変わるもの=追加・削除・改名を確認できる)
    std::error_code ec;
    auto now = std::filesystem::last_write_time(dir, ec);
    auto scanned = model_.ScannedMtime();
    if (!ec && scanned && now != *scanned) OnDirectoryChanged();
}

void FileListView::OnDirectoryChanged()
{
    if (stale_) return; // すでに反映待ち(続く変化はまとめて1回で反映される)
    stale_ = true;
    reload_due_ = std::max(std::chrono::steady_clock::now() + kAutoReloadDelay, next_reload_allowed_);
}

void FileListView::UpdateAutoReload(bool allow)
{
    if (!stale_ || !allow) return;
    auto start = std::chrono::steady_clock::now();
    if (start < reload_due_) return;

    // Reload()が成功すると、パス変更の通知の中でstale_が落ちる(その中で、走査より後の変更を
    // 見つけたらまた立つ)。失敗(ディレクトリが消えた等)した場合は通知が無いので、先に落として
    // おいて再試行はしない(次の変化の通知か、手動のリロードを待つ)。
    stale_ = false;
    (void)Reload();

    auto end = std::chrono::steady_clock::now();
    next_reload_allowed_ = end + std::max<std::chrono::steady_clock::duration>(kAutoReloadMinInterval, (end - start) * kAutoReloadCostFactor);
    if (stale_) reload_due_ = std::max(reload_due_, next_reload_allowed_);
}

void FileListView::SetDragGuard(std::function<bool()> guard)
{
    drag_guard_ = std::move(guard);
}

const std::vector<FileListView::DragEntry>& FileListView::BeginDrag()
{
    drag_entries_.clear();
    if (drag_guard_ && !drag_guard_()) return drag_entries_;

    // 画面表示順(MarkedEntries参照)。貼り付け先でも画面と同じ並びになる
    for (auto* entry_model : MarkedEntries()) {
        drag_entries_.push_back({entry_model->Path(), entry_model->IsDirectory()});
    }
    drag_source_dir_ = model_.Path();
    return drag_entries_;
}

void FileListView::EndDrag(bool accepted)
{
    auto entries = std::move(drag_entries_);
    drag_entries_.clear();
    if (!accepted || entries.empty()) return;

    // 宛先(Finderなど)はドロップを受理した後に非同期で移動/コピーを進めるため、受理直後の
    // ファイルシステムはまだ変わっていないことがある。ファイル監視の仕組みも無いので、少し待ってから
    // 実際の状態を見て後始末を決める(宛先が申告する操作の種類はあくまで自己申告なので、
    // 実際にファイルが動いたかは自分で確かめる)。
    auto dir = drag_source_dir_;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(kDropSettleSeconds * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
        // 待っている間に別ディレクトリへ移動していたら、そちらのマーク等には触れない
        if (model_.Path() != dir) return;

        auto moved = std::any_of(entries.begin(), entries.end(), [](const DragEntry& e) { return IsGone(e.path); });
        if (moved) {
            // 消えたファイルを一覧に反映する。カーソルは維持され、移されなかったファイルの
            // マークは残る。失敗(ディレクトリが読めない等)しても一覧は変わらないだけなので無視する。
            (void)Reload();
        }
        else {
            ClearMarks(); // 中身は変わらない(コピー等)のでマークだけ解除する
        }
    });
}

void FileListView::DrawHeader()
{
    NSRect bounds = impl_->header_view.bounds;
    // 背景も文字も設定の色(OSのテーマには従わない)。文字をOSの色(textColor)にすると、背景を暗くしたとき、
    // Lightのままだと黒文字が暗い背景に沈む
    [ToNSColor(Config::Background()) set];
    NSRectFill(bounds);

    NSString* path = @(model_.Path().c_str());
    NSDictionary* attrs = @{
        NSFontAttributeName: MakeFont(Config::FontSize() + 1),
        NSForegroundColorAttributeName: ToNSColor(Config::Color().Get(Config::Color::Type::NormalText)),
    };
    NSSize text_size = [path sizeWithAttributes:attrs];
    NSRect text_rect = NSMakeRect(kPadding, (bounds.size.height - text_size.height) / 2, bounds.size.width - kPadding * 2, text_size.height);
    [path drawInRect:text_rect withAttributes:attrs];
}

void FileListView::Draw(double min_y, double max_y)
{
    // 背景。スクロール領域の下(行が足りない部分)まで含めて、見えている範囲を全部塗る
    // (documentViewは、行数分と表示領域の高さの大きい方に揃えてある: Redraw)。塗らないと、
    // 後ろのウィンドウの背景(OSのテーマの色)が見える
    [ToNSColor(Config::Background()) set];
    NSRectFill(impl_->content_view.bounds);

    // リスト本体
    {
        auto dir_color = ToNSColor(Config::Color().Get(Config::Color::Type::Directory));
        auto file_color = ToNSColor(Config::Color().Get(Config::Color::Type::NormalFile));
        auto match_color = ToNSColor(Config::Color().Get(Config::Color::Type::SearchMatch));
        auto current_color = ToNSColor(Config::Color().Get(Config::Color::Type::SearchCurrent));
        NSFont* font = MakeFont(Config::FontSize());
        CGFloat row_height = RowHeight();

        // 描き直す範囲にかかる行だけ描く(行ごとにファイルの大きさを調べるので、全行描くと数万件で重い)
        auto size = (int)list_.size();
        int first = std::max(0, (int)std::floor(min_y / row_height));
        int last = std::min(size - 1, (int)std::ceil(max_y / row_height) - 1);
        for (int i = first; i <= last; i++) {
            auto& entry_model = list_[(size_t)i]->Model();
            CGFloat y = (CGFloat)i * row_height;
            NSRect row_rect = NSMakeRect(0, y, impl_->content_view.bounds.size.width, row_height);

            if (entry_model.IsMarked()) {
                [[NSColor colorWithRed:0 green:0.25 blue:0.5 alpha:1.0] set];
                NSRectFill(row_rect);
            }
            if (i == cursorIndex_ && focus_) {
                NSRect underline = NSMakeRect(row_rect.origin.x, row_rect.origin.y + row_rect.size.height - 2, row_rect.size.width, 2);
                [[NSColor colorWithRed:1 green:0 blue:0 alpha:1] set];
                NSRectFill(underline);
            }

            NSDictionary* attrs = @{
                NSFontAttributeName: font,
                NSForegroundColorAttributeName: entry_model.IsDirectory() ? dir_color : file_color,
            };

            std::string size_or_dir = entry_model.IsDirectory() ? "<DIR>" : FormatSize(entry_model.Size());
            NSString* right_text = [NSString stringWithFormat:@"%s  %s", size_or_dir.c_str(), entry_model.ModifiedTime().c_str()];
            NSSize right_size = [right_text sizeWithAttributes:attrs];
            NSRect right_rect = NSMakeRect(
                row_rect.origin.x + row_rect.size.width - right_size.width - kPadding,
                row_rect.origin.y + (row_height - right_size.height) / 2,
                right_size.width, right_size.height
            );
            [right_text drawInRect:right_rect withAttributes:attrs];

            NSString* name = @(entry_model.Name().c_str());
            NSSize name_size = [name sizeWithAttributes:attrs];
            NSRect name_rect = NSMakeRect(
                row_rect.origin.x + kPadding,
                row_rect.origin.y + (row_height - name_size.height) / 2,
                row_rect.size.width - right_size.width - kPadding * 3,
                name_size.height
            );

            // 検索で一致した行は、一致した部分(すべての出現)に背景色を付ける。カーソルのある行(今いるマッチ)は
            // 別の色にする。カーソルの下線と同じく、フォーカスのあるペインだけ
            auto* matches = name ? search_.RangesFor(i) : nullptr;
            if (matches) {
                NSMutableAttributedString* highlighted = [[NSMutableAttributedString alloc] initWithString:name attributes:attrs];
                NSColor* background = (i == cursorIndex_ && focus_) ? current_color : match_color;
                for (const auto& match : *matches) {
                    // 範囲外はNSRangeExceptionになるので、一覧が作り直された直後などの食い違いは捨てる
                    if (match.location + match.length > highlighted.length) continue;
                    [highlighted addAttribute:NSBackgroundColorAttributeName value:background range:NSMakeRange(match.location, match.length)];
                }
                [highlighted drawInRect:name_rect];
            }
            else {
                [name drawInRect:name_rect withAttributes:attrs];
            }
        }
    }
}

} // namespace miata::views

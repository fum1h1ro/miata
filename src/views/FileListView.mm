#import <AppKit/AppKit.h>
#include <algorithm>
#include <cmath>
#include <format>
#include <unordered_map>
#include "FileListView.h"
#include "../platform.h"
#include "../Config.h"
#include "NSColorUtil.h"

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
    if (self.owner) self.owner->Draw();
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
- (void)layoutChildren;
@end
@implementation _MiataFileListLayoutContainer
- (BOOL)isFlipped { return YES; }
- (void)layoutChildren
{
    CGFloat w = self.bounds.size.width;
    CGFloat h = self.bounds.size.height;
    self.headerView.frame = NSMakeRect(0, 0, w, self.headerHeight);
    self.scrollView.frame = NSMakeRect(0, self.headerHeight, w, MAX((CGFloat)0, h - self.headerHeight));
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
    constexpr CGFloat kPadding = 6;
    constexpr CGFloat kRowVerticalMargin = 8; // 行の上下に確保する余白の合計
    constexpr CGFloat kHeaderVerticalMargin = 13;

    // ファイル一覧・ヘッダーの行の高さは、フォントサイズに応じて動的に決める
    // (固定値のままだとフォントサイズを上げた時に行同士が重なってしまうため)。
    CGFloat RowHeight()
    {
        return Config::FontSize() + kRowVerticalMargin;
    }
    CGFloat HeaderHeight()
    {
        return Config::FontSize() + 1 + kHeaderVerticalMargin;
    }

    // Miata.config.set_font(name) で指定されたフォントを使う。未指定、または
    // 指定された名前が解決できない場合はシステムデフォルトフォントにフォールバックする。
    NSFont* MakeFont(CGFloat size)
    {
        auto& family = Config::FontFamily();
        if (!family.empty()) {
            NSFont* f = [NSFont fontWithName:@(family.c_str()) size:size];
            if (f) return f;
        }
        return [NSFont systemFontOfSize:size];
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

    // 実体が確実に無くなっている場合のみtrue。エラーで判定できない場合や、壊れた
    // シンボリックリンク(リンク自体は残っている)はfalse。
    bool IsGone(const std::filesystem::path& path)
    {
        std::error_code ec;
        return std::filesystem::symlink_status(path, ec).type() == std::filesystem::file_type::not_found;
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
            .subscribe([this](const std::filesystem::path&) {
                Fetch();
                // Reload()経由の通知ならカーソルを復元する。ディレクトリ移動(JumpTo)は先頭から
                cursorIndex_ = reload_memo_ ? RestoreCursor(*reload_memo_) : 0;
                Redraw();
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
}

std::expected<void, std::string> FileListView::Reload(std::optional<std::filesystem::path> cursor_to)
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

void FileListView::SetDragGuard(std::function<bool()> guard)
{
    drag_guard_ = std::move(guard);
}

const std::vector<FileListView::DragEntry>& FileListView::BeginDrag()
{
    drag_entries_.clear();
    if (drag_guard_ && !drag_guard_()) return drag_entries_;

    // list_は画面表示順(ソート済み)。貼り付け先でも画面と同じ並びになるようこの順で集める
    for (auto* entry : list_) {
        auto& entry_model = entry->Model();
        if (entry_model.IsMarked()) {
            drag_entries_.push_back({entry_model.Path(), entry_model.IsDirectory()});
        }
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
            model_.ClearMarks(); // 中身は変わらない(コピー等)のでマークだけ解除する
            Redraw();
        }
    });
}

void FileListView::DrawHeader()
{
    NSRect bounds = impl_->header_view.bounds;
    auto bg = pl_get_color(pl_color_type::window_background_color);
    [ToNSColor(bg) set];
    NSRectFill(bounds);

    NSString* path = @(model_.Path().c_str());
    NSDictionary* attrs = @{
        NSFontAttributeName: MakeFont(Config::FontSize() + 1),
        NSForegroundColorAttributeName: ToNSColor(pl_get_color(pl_color_type::text_color)),
    };
    NSSize text_size = [path sizeWithAttributes:attrs];
    NSRect text_rect = NSMakeRect(kPadding, (bounds.size.height - text_size.height) / 2, bounds.size.width - kPadding * 2, text_size.height);
    [path drawInRect:text_rect withAttributes:attrs];
}

void FileListView::Draw()
{
    // リスト本体
    {
        auto dir_color = ToNSColor(Config::Color().Get(Config::Color::Type::Directory));
        auto file_color = ToNSColor(Config::Color().Get(Config::Color::Type::NormalFile));
        NSFont* font = MakeFont(Config::FontSize());
        CGFloat row_height = RowHeight();

        auto size = (int)list_.size();
        for (int i = 0; i < size; i++) {
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
            [name drawInRect:name_rect withAttributes:attrs];
        }
    }
}

} // namespace miata::views

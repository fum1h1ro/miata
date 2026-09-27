#import <AppKit/AppKit.h>
#include <algorithm>
#include <format>
#include "FileListView.h"
#include "../platform.h"
#include "../Config.h"

@interface _MiataFileListNSView : NSView
@property (nonatomic, assign) miata::views::FileListView* owner;
@end
@implementation _MiataFileListNSView
- (BOOL)isFlipped { return YES; }
- (void)drawRect:(NSRect)dirtyRect
{
    if (self.owner) self.owner->Draw();
}
@end

// ヘッダー(パス表示)とスクロール可能なリストを縦に並べるだけの単純なコンテナ。
@interface _MiataFileListContainer : NSView
@end
@implementation _MiataFileListContainer
- (BOOL)isFlipped { return YES; }
@end

namespace miata::views {

namespace {
    constexpr CGFloat kHeaderHeight = 26;
    constexpr CGFloat kRowHeight = 20;
    constexpr CGFloat kPadding = 6;

    NSColor* ToNSColor(const Color4f& c)
    {
        return [NSColor colorWithRed:c.r green:c.g blue:c.b alpha:c.a];
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
}

struct FileListView::Impl {
    _MiataFileListContainer* container = nil;
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
    impl_->container = [[_MiataFileListContainer alloc] initWithFrame:NSMakeRect(0, 0, 200, 200)];

    impl_->header_view = [[_MiataFileListContainer alloc] initWithFrame:NSMakeRect(0, 0, 200, kHeaderHeight)];
    impl_->header_view.autoresizingMask = NSViewWidthSizable;
    [impl_->container addSubview:impl_->header_view];

    impl_->content_view = [[_MiataFileListNSView alloc] initWithFrame:NSMakeRect(0, 0, 200, 200 - kHeaderHeight)];
    impl_->content_view.owner = this;

    impl_->scroll_view = [[NSScrollView alloc] initWithFrame:NSMakeRect(0, kHeaderHeight, 200, 200 - kHeaderHeight)];
    impl_->scroll_view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    impl_->scroll_view.hasVerticalScroller = YES;
    impl_->scroll_view.hasHorizontalScroller = NO;
    impl_->scroll_view.drawsBackground = NO;
    impl_->scroll_view.documentView = impl_->content_view;
    [impl_->container addSubview:impl_->scroll_view];

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
                cursorIndex_ = 0;
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
            if (x->Model().Ext() != y->Model().Ext()) return x->Model().Ext() < y->Model().Ext();
            return x->Model().Name() < y->Model().Name();
        case SortKey::Name:
        default:
            return x->Model().Name() < y->Model().Name();
        }
    });
}

void FileListView::Redraw()
{
    auto size = (int)list_.size();
    if (cursorIndex_ < 0) cursorIndex_ = 0;
    if (cursorIndex_ >= size) cursorIndex_ = std::max(0, size - 1);

    NSRect content_frame = impl_->content_view.frame;
    CGFloat needed_height = std::max((CGFloat)size * kRowHeight, impl_->scroll_view.contentSize.height);
    CGFloat needed_width = impl_->scroll_view.contentSize.width;
    if (content_frame.size.height != needed_height || content_frame.size.width != needed_width) {
        content_frame.size.height = needed_height;
        content_frame.size.width = needed_width;
        impl_->content_view.frame = content_frame;
    }

    if (focus_ && size > 0) {
        NSRect row_rect = NSMakeRect(0, (CGFloat)cursorIndex_ * kRowHeight, content_frame.size.width, kRowHeight);
        [impl_->content_view scrollRectToVisible:row_rect];
    }

    [impl_->header_view setNeedsDisplay:YES];
    [impl_->content_view setNeedsDisplay:YES];
}

void FileListView::Draw()
{
    // ヘッダー: パス表示
    {
        NSRect bounds = impl_->header_view.bounds;
        auto bg = pl_get_color(pl_color_type::window_background_color);
        [ToNSColor(bg) set];
        NSRectFill(bounds);

        NSString* path = @(model_.Path().c_str());
        NSDictionary* attrs = @{
            NSFontAttributeName: [NSFont systemFontOfSize:13],
            NSForegroundColorAttributeName: ToNSColor(pl_get_color(pl_color_type::text_color)),
        };
        NSRect text_rect = NSMakeRect(kPadding, (bounds.size.height - 16) / 2, bounds.size.width - kPadding * 2, 16);
        [path drawInRect:text_rect withAttributes:attrs];
    }

    // リスト本体
    {
        auto dir_color = ToNSColor(Config::Color().Get(Config::Color::Type::Directory));
        auto file_color = ToNSColor(Config::Color().Get(Config::Color::Type::NormalFile));
        NSFont* font = [NSFont systemFontOfSize:12];

        auto size = (int)list_.size();
        for (int i = 0; i < size; i++) {
            auto& entry_model = list_[(size_t)i]->Model();
            CGFloat y = (CGFloat)i * kRowHeight;
            NSRect row_rect = NSMakeRect(0, y, impl_->content_view.bounds.size.width, kRowHeight);

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
                row_rect.origin.y + (kRowHeight - right_size.height) / 2,
                right_size.width, right_size.height
            );
            [right_text drawInRect:right_rect withAttributes:attrs];

            NSString* name = @(entry_model.Name().c_str());
            NSRect name_rect = NSMakeRect(
                row_rect.origin.x + kPadding,
                row_rect.origin.y + (kRowHeight - 16) / 2,
                row_rect.size.width - right_size.width - kPadding * 3,
                16
            );
            [name drawInRect:name_rect withAttributes:attrs];
        }
    }
}

} // namespace miata::views

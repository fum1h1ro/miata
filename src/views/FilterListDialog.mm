#import <AppKit/AppKit.h>
#include <algorithm>
#include <cmath>
#include "FilterListDialog.h"
#include "../FzfFilter.h"
#include "../platform.h"
#include "NSColorUtil.h"

// フィルタ欄の上下矢印/Enter/Escapeだけを横取りし、通常の文字入力とIME変換中の操作は
// AppKit標準の経路(素通し)に任せる。doCommandBySelector:はIME変換中は呼ばれないため、
// 変換候補選択中の矢印キー/Enterは自然にIME側に渡る。
@interface _MiataFilterFieldDelegate : NSObject <NSTextFieldDelegate>
@property (nonatomic, assign) miata::views::FilterListDialog* owner;
@end
@implementation _MiataFilterFieldDelegate
- (BOOL)control:(NSControl*)control textView:(NSTextView*)textView doCommandBySelector:(SEL)commandSelector
{
    if (!self.owner) return NO;
    auto* owner = self.owner;
    if (commandSelector == @selector(moveUp:)) {
        owner->HandleMoveSelection(-1);
        return YES;
    }
    if (commandSelector == @selector(moveDown:)) {
        owner->HandleMoveSelection(1);
        return YES;
    }
    if (commandSelector == @selector(insertNewline:)) {
        // ダイアログを閉じる処理はこの呼び出し自身のコールスタックを抜けてから行う。
        // このdelegateはダイアログのImplが保持しており、閉じる過程(OnClose)で一緒に
        // 破棄されるため、Dialog.mmの_MiataControlTarget::fire:と同じ理由で
        // 次のランループへ逃がす(自分自身の呼び出し中に自分自身が破棄されるのを避ける)。
        dispatch_async(dispatch_get_main_queue(), ^{
            owner->HandleConfirm();
        });
        return YES;
    }
    if (commandSelector == @selector(cancelOperation:)) {
        dispatch_async(dispatch_get_main_queue(), ^{
            owner->HandleCancel();
        });
        return YES;
    }
    return NO;
}
- (void)controlTextDidChange:(NSNotification*)notification
{
    if (!self.owner) return;
    NSTextField* field = (NSTextField*)notification.object;
    self.owner->HandleQueryChanged(std::string(field.stringValue.UTF8String));
}
@end

// クリッピング付きの一覧描画ビュー。drawRect:のdirtyRectの範囲だけを描画対象にすることで、
// 候補が数千件あっても実際に見えている行数分しか描画コストがかからないようにする
// (FileListView::Draw()は行数が少ない前提で全行を無条件描画しており、それとの意図的な差分)。
@interface _MiataFilterListNSView : NSView
@property (nonatomic, assign) miata::views::FilterListDialog* owner;
@end
@implementation _MiataFilterListNSView
- (BOOL)isFlipped { return YES; }
- (void)drawRect:(NSRect)dirtyRect
{
    if (self.owner) self.owner->DrawList((float)NSMinY(dirtyRect), (float)NSMaxY(dirtyRect));
}
@end

namespace miata::views {

namespace {
    constexpr CGFloat kRowHeight = 22;
    constexpr CGFloat kListHeight = 330;
}

struct FilterListDialog::Impl {
    _MiataFilterFieldDelegate* delegate = nil;
    NSScrollView* scroll_view = nil;
    _MiataFilterListNSView* list_view = nil;
};

FilterListDialog::FilterListDialog(std::function<void(IDialog&)> on_close, arguments args)
    : IDialog(typeid(this).name(), std::move(on_close)), args_(std::move(args))
{
}

FilterListDialog::~FilterListDialog() = default;

void FilterListDialog::OnOpen()
{
    panel_.Show(args_.title_, args_.message_, GetIdealSize());
    panel_.AddTextField("");

    impl_ = std::make_unique<Impl>();
    impl_->delegate = [[_MiataFilterFieldDelegate alloc] init];
    impl_->delegate.owner = this;
    panel_.SetTextFieldDelegate((__bridge void*)impl_->delegate);

    impl_->list_view = [[_MiataFilterListNSView alloc] initWithFrame:NSMakeRect(0, 0, 1, 1)];
    impl_->list_view.owner = this;

    impl_->scroll_view = [[NSScrollView alloc] initWithFrame:NSMakeRect(0, 0, 1, kListHeight)];
    impl_->scroll_view.hasVerticalScroller = YES;
    impl_->scroll_view.hasHorizontalScroller = NO;
    impl_->scroll_view.drawsBackground = NO;
    impl_->scroll_view.documentView = impl_->list_view;

    panel_.AddCustomView((__bridge void*)impl_->scroll_view, (float)kListHeight);

    filter_ = std::make_unique<FzfFilter>(args_.items_);
    RebuildList("");
}

void FilterListDialog::OnClose()
{
    filter_.reset(); // 一時ファイルをここで確実に破棄する(LuaのGCタイミングに依存しない)
    impl_.reset();
}

void FilterListDialog::RebuildList(const std::string& query)
{
    filtered_ = filter_->Filter(query);
    selected_index_ = filtered_.empty() ? -1 : 0;
    UpdateListLayout();
}

void FilterListDialog::UpdateListLayout()
{
    if (!impl_ || !impl_->scroll_view) return;

    CGFloat width = impl_->scroll_view.contentSize.width;
    CGFloat needed_height = std::max((CGFloat)filtered_.size() * kRowHeight, impl_->scroll_view.contentSize.height);
    NSRect frame = impl_->list_view.frame;
    frame.size.width = width;
    frame.size.height = needed_height;
    impl_->list_view.frame = frame;

    if (selected_index_ >= 0) {
        NSRect row_rect = NSMakeRect(0, (CGFloat)selected_index_ * kRowHeight, width, kRowHeight);
        [impl_->list_view scrollRectToVisible:row_rect];
    }
    [impl_->list_view setNeedsDisplay:YES];
}

void FilterListDialog::HandleQueryChanged(const std::string& query)
{
    RebuildList(query);
}

void FilterListDialog::HandleMoveSelection(int delta)
{
    if (filtered_.empty()) return;
    selected_index_ = std::clamp(selected_index_ + delta, 0, (int)filtered_.size() - 1);
    UpdateListLayout();
}

void FilterListDialog::HandleConfirm()
{
    if (selected_index_ >= 0 && selected_index_ < (int)filtered_.size()) {
        result_ = filtered_[(size_t)selected_index_];
    }
    CloseDialog();
}

void FilterListDialog::HandleCancel()
{
    result_ = std::nullopt;
    CloseDialog();
}

void FilterListDialog::DrawList(float visible_min_y, float visible_max_y)
{
    if (!impl_ || filtered_.empty()) return;

    int first = std::max(0, (int)std::floor((double)visible_min_y / kRowHeight));
    int last = std::min((int)filtered_.size() - 1, (int)std::ceil((double)visible_max_y / kRowHeight));
    if (first > last) return;

    NSColor* text_color = ToNSColor(pl_get_color(pl_color_type::text_color));
    NSColor* highlight_color = ToNSColor(pl_get_color(pl_color_type::selected_content_background_color));
    NSDictionary* attrs = @{
        NSFontAttributeName: [NSFont systemFontOfSize:13],
        NSForegroundColorAttributeName: text_color,
    };
    CGFloat width = impl_->list_view.bounds.size.width;

    for (int i = first; i <= last; ++i) {
        CGFloat y = (CGFloat)i * kRowHeight;
        NSRect row_rect = NSMakeRect(0, y, width, kRowHeight);
        if (i == selected_index_) {
            [highlight_color set];
            NSRectFill(row_rect);
        }

        NSString* text = @(filtered_[(size_t)i].c_str());
        NSSize text_size = [text sizeWithAttributes:attrs];
        NSRect text_rect = NSMakeRect(6, y + (kRowHeight - text_size.height) / 2, width - 12, text_size.height);
        [text drawInRect:text_rect withAttributes:attrs];
    }
}

} // namespace miata::views

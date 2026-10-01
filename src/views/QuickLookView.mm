#import <AppKit/AppKit.h>
#import <Quartz/Quartz.h>
#include <system_error>
#include "QuickLookView.h"
#include "../Config.h"
#include "NSColorUtil.h"

// プレビューを載せる覆い。中のQLPreviewViewにマウスを渡さず、一覧が透けて見えないよう不透明に塗る。
// 色は一覧と同じ(設定の背景色)。読み込み中や空のときに、ここが見える。
@interface _MiataQuickLookShield : NSView
@end
@implementation _MiataQuickLookShield
- (BOOL)isOpaque { return YES; }
- (void)drawRect:(NSRect)dirtyRect
{
    [miata::views::ToNSColor(miata::Config::Background()) setFill];
    NSRectFill(dirtyRect);
}
// マウスは常にこのビューが受ける(何もしない)。中のQLPreviewViewがヒットしないので、クリックしても
// first responderは動かず、キー入力はこれまで通りMiataRootViewに届く。
- (NSView*)hitTest:(NSPoint)point
{
    return [super hitTest:point] ? self : nil;
}
@end

namespace miata::views {

struct QuickLookView::Impl {
    _MiataQuickLookShield* shield = nil;
    QLPreviewView* preview = nil; // 表示している間だけ持つ
};

QuickLookView::QuickLookView() : impl_(std::make_unique<Impl>())
{
    impl_->shield = [[_MiataQuickLookShield alloc] initWithFrame:NSMakeRect(0, 0, 200, 200)];
    impl_->shield.hidden = YES;
}

// 表示中のまま破棄されても、QLPreviewViewは覆いごと親(ウィンドウ)が持ち続け、プロセスの終了とともに
// 消えるので、ここでは何もしない(closeしないまま終了しても問題ない)。
QuickLookView::~QuickLookView() = default;

void* QuickLookView::NativeView() const
{
    return (__bridge void*)impl_->shield;
}

void QuickLookView::Show()
{
    if (impl_->preview) return;
    impl_->preview = [[QLPreviewView alloc] initWithFrame:impl_->shield.bounds style:QLPreviewViewStyleNormal];
    impl_->preview.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    // closeはHide()だけが呼ぶ。既定(YES)だと、ウィンドウが閉じるときにQuickLook側も自動でcloseするので、
    // その後のHide()のcloseが二重になる(下記の理由で異常終了する)。
    impl_->preview.shouldCloseWithWindow = NO;
    [impl_->shield addSubview:impl_->preview];
    impl_->shield.hidden = NO;
}

void QuickLookView::Hide()
{
    if (!impl_->preview) return;
    // QLPreviewViewのcloseには、守らないとQuickLookのアサートでプロセスが異常終了する決まりがある(実測):
    //   ・closeは、ウィンドウに載っている間に、1回だけ呼ぶ(二重に呼ぶ/ウィンドウから外れた後に呼ぶと異常終了)
    //   ・closeした後のビューに、previewItemの設定など何も触れない
    // なので、外す前に呼び、呼んだらすぐ手放す。作り直す費用は小さい(1回0.5ms未満)ので、隠している間は持たない。
    // (ウィンドウに載っていなければ、まだ何も始まっていないので、closeせずに手放してよい)
    if (impl_->preview.window) [impl_->preview close];
    [impl_->preview removeFromSuperview];
    impl_->preview = nil;
    impl_->shield.hidden = YES;
}

bool QuickLookView::IsShown() const
{
    return impl_->preview != nil;
}

void QuickLookView::SetFile(const std::optional<std::filesystem::path>& path)
{
    if (!impl_->preview) return;
    NSURL* url = nil;
    if (path) {
        // パスをNSStringに変換せず、バイト列から直接URLを作る(変換できない名前でも例外にならない)
        std::error_code ec;
        bool is_directory = std::filesystem::is_directory(*path, ec);
        url = [NSURL fileURLWithFileSystemRepresentation:path->c_str() isDirectory:is_directory relativeToURL:nil];
    }
    impl_->preview.previewItem = url;
}

} // namespace miata::views

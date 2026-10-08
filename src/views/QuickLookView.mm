#import <AppKit/AppKit.h>
#import <Quartz/Quartz.h>
#import <WebKit/WebKit.h>
#include <algorithm>
#include <system_error>
#include "QuickLookView.h"
#include "../Config.h"
#include "NSColorUtil.h"

namespace {

// プレビューの中の、倍率を指定できるビュー(無ければnil)。QLPreviewViewの中身の作りは、種類で違う(macOS 27で実測):
//   ・WebKitで描かれる種類(xlsx・docx・csv・html・svg): QLWeb2View(WKWebViewのサブクラス)。アプリ内で描かれる
//   ・テキスト系(txt・md・rtf): QLTextScrollView(NSScrollView。documentViewはNSTextView)。アプリ内で描かれる
//   ・画像・PDF・json: NSRemoteView。別プロセスで描かれ、倍率を指定する手段が無い(非公開のzoomFactor等も効かない)
// 非公開のクラス名には頼らず、公開の基底クラスで探す(OSの更新で作りが変わったら、見つからなくなる=ズームできなくなる
// だけで、壊れはしない)。
NSView* FindZoomTarget(NSView* root)
{
    for (NSView* child in root.subviews) {
        if ([child isKindOfClass:[WKWebView class]]) return child;
        if ([child isKindOfClass:[NSScrollView class]] &&
            [static_cast<NSScrollView*>(child).documentView isKindOfClass:[NSTextView class]]) {
            return child;
        }
        if (NSView* found = FindZoomTarget(child)) return found;
    }
    return nil;
}

double Magnification(NSView* target)
{
    if ([target isKindOfClass:[WKWebView class]]) return static_cast<WKWebView*>(target).magnification;
    return static_cast<NSScrollView*>(target).magnification;
}

void SetMagnification(NSView* target, double factor)
{
    if ([target isKindOfClass:[WKWebView class]]) {
        static_cast<WKWebView*>(target).magnification = factor;
    }
    else {
        static_cast<NSScrollView*>(target).magnification = factor;
    }
}

// ピンチを受けられるようにする。テキスト系のNSScrollViewは、既定ではallowsMagnification = NOで、ピンチを受けない
void AllowMagnification(NSView* target)
{
    if ([target isKindOfClass:[WKWebView class]]) {
        static_cast<WKWebView*>(target).allowsMagnification = YES;
    }
    else {
        static_cast<NSScrollView*>(target).allowsMagnification = YES;
    }
}

} // namespace

// プレビューを載せる覆い。中のQLPreviewViewにマウスを渡さず、一覧が透けて見えないよう不透明に塗る。
// 色は一覧と同じ(設定の背景色)。読み込み中や空のときに、ここが見える。
@interface _MiataQuickLookShield : NSView
@end
@implementation _MiataQuickLookShield {
    BOOL forwarding_; // ピンチを転送している最中(再入を止める)
}
- (BOOL)isOpaque { return YES; }
- (void)drawRect:(NSRect)dirtyRect
{
    // dirtyRectは、自分の範囲を超えて渡されることがある(親をまるごと描くとき。macOS 14以降、ビューは既定で
    // 自分の範囲に描画を切り詰めない)。そのまま塗ると、隣のビュー(一覧)を塗りつぶしてしまう
    NSRectFill(NSIntersectionRect(dirtyRect, self.bounds));
}
// マウスは常にこのビューが受ける(何もしない)。中のQLPreviewViewがヒットしないので、クリックしても
// first responderは動かず、キー入力はこれまで通りMiataRootViewに届く。
- (NSView*)hitTest:(NSPoint)point
{
    return [super hitTest:point] ? self : nil;
}
// トラックパッドのピンチ(拡大・縮小)は、ここで受けて、倍率を指定できる中のビュー(FindZoomTarget)へ渡す。
// QLPreviewView自身はmagnifyWithEvent:を実装していない(渡しても、nextResponder=この覆いへ戻ってくるだけ)ので、
// 中のビューへ直接渡す。ズームできない種類(別プロセスで描かれる画像・PDFなど)では、これまで通り何もしない。
// 中のビューが受けずにnextResponderへ戻してきたときも、戻り先はこの覆いなので、forwarding_で再入を止める
// (止めないと、覆い→中のビュー→覆い…と、無限に再帰する)。
- (void)magnifyWithEvent:(NSEvent*)event
{
    if (forwarding_) return;
    NSView* target = FindZoomTarget(self);
    if (!target) return;
    forwarding_ = YES;
    AllowMagnification(target);
    [target magnifyWithEvent:event];
    forwarding_ = NO;
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

std::optional<double> QuickLookView::Zoom() const
{
    if (!impl_->preview) return std::nullopt;
    NSView* target = FindZoomTarget(impl_->preview);
    if (!target) return std::nullopt;
    return Magnification(target);
}

std::optional<double> QuickLookView::SetZoom(double factor)
{
    if (!impl_->preview) return std::nullopt;
    NSView* target = FindZoomTarget(impl_->preview);
    if (!target) return std::nullopt;
    SetMagnification(target, std::clamp(factor, kMinZoom, kMaxZoom));
    // 中のビューが、さらに範囲を狭めることがあるので、実際になった倍率を読み戻す
    return Magnification(target);
}

} // namespace miata::views

#import <AppKit/AppKit.h>
#include <algorithm>
#include <cmath>
#include "ProgressOverlay.h"
#include "../Config.h"
#include "NSColorUtil.h"
#include "ViewMetrics.h"

namespace {
    constexpr CGFloat kMargin = 12;         // ウィンドウの右端・上端から
    constexpr CGFloat kBaseWidth = 340;     // パネルの幅(フォントが大きいときは広げる。狭い窓では縮める)
    constexpr CGFloat kStackGap = 8;        // パネルの間
    constexpr CGFloat kCornerRadius = 8;
    constexpr CGFloat kPadding = 8;         // パネルの内側の余白
    constexpr CGFloat kRowGap = 4;          // 行の間
    constexpr CGFloat kBackgroundAlpha = 0.85; // 背景の透過(背景色の alpha は、設定では使わない方針なので、ここで固定する)
    constexpr CGFloat kBorderAlpha = 0.25;
    constexpr CGFloat kDetailAlpha = 0.8;
    constexpr CGFloat kTrackAlpha = 0.25;   // バーの溝
    constexpr CGFloat kPulseWidthRatio = 0.3; // 割合が不定のときの、バーの区間の幅(溝に対する比)
    constexpr CGFloat kDetailFontScale = 0.85;

    using miata::views::LineHeight;

    CGFloat BarHeight()
    {
        return std::max<CGFloat>(4, std::round(miata::Config::FontSize() / 3));
    }

    NSColor* BarColor()
    {
        return miata::views::ToNSColor(miata::Config::Color().Get(miata::Config::Color::Type::Directory));
    }

    // フォントが大きいほど文字が広がるので、パネルも広げる(20pt までは、基準の幅)
    CGFloat PanelWidthFor(CGFloat host_width)
    {
        CGFloat base = kBaseWidth * std::max<CGFloat>(1, miata::Config::FontSize() / 20);
        return std::max<CGFloat>(0, std::min(base, host_width - 2 * kMargin));
    }
}

// パネル 1 枚。自前で描く(設定の色だけで描くので、OS の Light / Dark に依らない)。クリックは受けない。
@interface _MiataProgressPanelView : NSView
@property (nonatomic, copy) NSString* title;
@property (nonatomic, copy) NSString* detail;
@property (nonatomic, assign) double fraction; // 負 = 不定
@property (nonatomic, assign) double pulse;
@property (nonatomic, assign) double panelAlpha;
@end
@implementation _MiataProgressPanelView
- (BOOL)isFlipped { return YES; }
- (BOOL)isOpaque { return NO; }
// マウスは、下の一覧に通す(first responder も取らない: acceptsFirstResponder は既定の NO)
- (NSView*)hitTest:(NSPoint)point { return nil; }
- (BOOL)isAccessibilityElement { return YES; }
- (NSAccessibilityRole)accessibilityRole { return NSAccessibilityProgressIndicatorRole; }
- (NSString*)accessibilityLabel { return [NSString stringWithFormat:@"%@ %@", self.title ?: @"", self.detail ?: @""]; }
- (id)accessibilityValue { return self.fraction >= 0 ? @(self.fraction) : nil; }

- (void)drawRect:(NSRect)dirtyRect
{
    using namespace miata::views;
    [NSGraphicsContext saveGraphicsState];
    // 渡された範囲にだけ描く(一部だけが渡されたとき、残りを描き直さない。空や、自分の範囲と重ならない範囲なら、何も描かれない)。
    // dirtyRect は、自分の範囲を超えて渡されることがある(親をまるごと描くとき。macOS 14 以降)が、描くものはすべて自分の範囲の中に
    // 収まるので、外には描かない(dirtyRect そのものを塗ると、隣のビューを塗りつぶす。ここは、そうしない)
    NSRectClip(dirtyRect);
    // フェード(以下のすべてに掛かる)。範囲外の値は、CG が 0〜1 に丸める
    CGContextSetAlpha(NSGraphicsContext.currentContext.CGContext, self.panelAlpha);

    NSRect bounds = self.bounds;
    NSColor* text_color = NormalTextColor();
    NSColor* bar_color = BarColor();
    // 背景: 設定の背景色の RGB に、固定の透過(下の一覧が、透けて見える)。半透明なので、SourceOver で重ねる
    // (NSRectFill は Copy 合成で、下を置き換えてしまう。NSBezierPath の fill は SourceOver)
    NSBezierPath* shape = [NSBezierPath bezierPathWithRoundedRect:NSInsetRect(bounds, 0.5, 0.5) xRadius:kCornerRadius yRadius:kCornerRadius];
    [[ToNSColor(miata::Config::Background()) colorWithAlphaComponent:kBackgroundAlpha] setFill];
    [shape fill];
    shape.lineWidth = 1;
    [[text_color colorWithAlphaComponent:kBorderAlpha] setStroke];
    [shape stroke];

    // 縦の積み方(余白・題・バー・名前・余白)は、PanelHeight() と同じ。片方を直すときは、もう片方も
    CGFloat size = miata::Config::FontSize();
    NSFont* title_font = MakeFont(size);
    NSFont* detail_font = MakeFont(size * kDetailFontScale);
    CGFloat inner_width = bounds.size.width - 2 * kPadding;
    CGFloat y = kPadding;

    // 1 行目: 「コピー  3/12 · 45%」。幅に収まらなければ、末尾を省く
    NSMutableParagraphStyle* tail = [[NSMutableParagraphStyle alloc] init];
    tail.lineBreakMode = NSLineBreakByTruncatingTail;
    CGFloat title_height = LineHeight(title_font);
    [self.title drawInRect:NSMakeRect(kPadding, y, inner_width, title_height)
            withAttributes:@{NSFontAttributeName: title_font, NSForegroundColorAttributeName: text_color, NSParagraphStyleAttributeName: tail}];
    y += title_height + kRowGap;

    // バー: 溝(薄い)の上に、割合の分だけ塗る。割合が不定なら、溝の中を往復する区間
    CGFloat bar_height = BarHeight();
    NSRect track = NSMakeRect(kPadding, y, inner_width, bar_height);
    NSBezierPath* track_path = [NSBezierPath bezierPathWithRoundedRect:track xRadius:bar_height / 2 yRadius:bar_height / 2];
    [[bar_color colorWithAlphaComponent:kTrackAlpha] setFill];
    [track_path fill];
    [NSGraphicsContext saveGraphicsState];
    [track_path addClip];
    NSRect fill;
    if (self.fraction >= 0) {
        fill = NSMakeRect(track.origin.x, track.origin.y, std::round(track.size.width * std::clamp(self.fraction, 0.0, 1.0)), bar_height);
    }
    else {
        CGFloat segment = std::round(track.size.width * kPulseWidthRatio);
        double t = self.pulse < 0.5 ? self.pulse * 2 : (1 - self.pulse) * 2; // 三角波(0 → 1 → 0)
        fill = NSMakeRect(track.origin.x + std::round((track.size.width - segment) * t), track.origin.y, segment, bar_height);
    }
    [bar_color setFill];
    NSRectFillUsingOperation(fill, NSCompositingOperationSourceOver);
    [NSGraphicsContext restoreGraphicsState];
    y += bar_height + kRowGap;

    // 3 行目: いま処理している名前(長ければ、中ほどを省いて、拡張子を残す)
    NSMutableParagraphStyle* middle = [[NSMutableParagraphStyle alloc] init];
    middle.lineBreakMode = NSLineBreakByTruncatingMiddle;
    [self.detail drawInRect:NSMakeRect(kPadding, y, inner_width, LineHeight(detail_font))
             withAttributes:@{NSFontAttributeName: detail_font,
                              NSForegroundColorAttributeName: [text_color colorWithAlphaComponent:kDetailAlpha],
                              NSParagraphStyleAttributeName: middle}];

    [NSGraphicsContext restoreGraphicsState];
}
@end

// 全面の、素通しのホスト。パネルを、右上から縦に積む。窓のリサイズには、自分の大きさが変わったときに、明示的に並べ直して追従する
// (flipped のビューでは、autoresizing の Y マージンの意味が反転する癖があり、このコードベースは、明示計算で並べる)。
@interface _MiataProgressHostView : NSView
- (void)layoutPanels;
@end
@implementation _MiataProgressHostView
- (BOOL)isFlipped { return YES; }
- (BOOL)isOpaque { return NO; }
// どこでも、マウスを受けない。イベントは、背面のビュー(ファイル一覧)に届く(クリック・ドラッグ・スクロールホイール)
- (NSView*)hitTest:(NSPoint)point { return nil; }
- (void)setFrameSize:(NSSize)newSize
{
    [super setFrameSize:newSize];
    [self layoutPanels];
}
- (void)layoutPanels
{
    CGFloat width = PanelWidthFor(self.bounds.size.width);
    CGFloat height = miata::views::ProgressOverlay::PanelHeight();
    CGFloat x = std::floor(self.bounds.size.width - kMargin - width);
    CGFloat y = kMargin;
    for (NSView* panel in self.subviews) {
        NSRect frame = NSMakeRect(x, y, width, height);
        if (!NSEqualRects(panel.frame, frame)) panel.frame = frame;
        y += height + kStackGap;
    }
}
@end

namespace miata::views {

struct ProgressOverlay::Impl {
    _MiataProgressHostView* host = nil;
    std::vector<ProgressPanel> shown; // いま画面に出している内容
};

ProgressOverlay::ProgressOverlay() : impl_(std::make_unique<Impl>())
{
    impl_->host = [[_MiataProgressHostView alloc] initWithFrame:NSZeroRect]; // 大きさは、View が、窓に足すときに決める
    // layer-backed にする: パネルの更新が、下の一覧の描き直しを起こさず、一覧のスクロール(copiesOnScroll)にも、残像を残さない。
    // 重なる別のビュー(NSScrollView・QLPreviewView)と、安全に合成される
    impl_->host.wantsLayer = YES;
    impl_->host.hidden = YES;
}

ProgressOverlay::~ProgressOverlay() = default;

void* ProgressOverlay::NativeView() const
{
    return (__bridge void*)impl_->host;
}

// パネル 1 枚の高さ。縦の積み方は、drawRect: と同じ
double ProgressOverlay::PanelHeight()
{
    CGFloat size = Config::FontSize();
    return std::ceil(kPadding + LineHeight(MakeFont(size)) + kRowGap + BarHeight() + kRowGap + LineHeight(MakeFont(size * kDetailFontScale)) + kPadding);
}

void ProgressOverlay::Update(const std::vector<ProgressPanel>& panels)
{
    if (panels == impl_->shown) return; // 同じ内容なら、AppKit に触らない
    _MiataProgressHostView* host = impl_->host;

    bool relayout = false;
    while (host.subviews.count > panels.size()) {
        [host.subviews.lastObject removeFromSuperview];
        relayout = true;
    }
    while (host.subviews.count < panels.size()) {
        _MiataProgressPanelView* panel = [[_MiataProgressPanelView alloc] initWithFrame:NSMakeRect(0, 0, 0, 0)];
        [host addSubview:panel];
        relayout = true;
    }
    for (size_t i = 0; i < panels.size(); ++i) {
        auto* view = (_MiataProgressPanelView*)host.subviews[i];
        auto& panel = panels[i];
        // 変わったものがあるパネルだけ、描き直す(同じ位置のパネルの、前の内容と比べる)
        if (i >= impl_->shown.size() || impl_->shown[i] != panel) {
            view.title = @(panel.title.c_str());
            view.detail = @(panel.detail.c_str());
            view.fraction = panel.fraction ? *panel.fraction : -1.0;
            view.pulse = panel.pulse;
            view.panelAlpha = panel.alpha;
            view.needsDisplay = YES;
        }
    }
    impl_->shown = panels;
    host.hidden = panels.empty();
    if (relayout) [host layoutPanels];
}

}

#import <AppKit/AppKit.h>
#include <algorithm>
#include <cmath>
#include "StatusBar.h"
#include "NSColorUtil.h"
#include "ViewMetrics.h"
#include "../Config.h"

namespace {
    // 左右の文字の間の、最小の隙間(pt)
    constexpr CGFloat kGap = 12;
    // 右(マークの要約)が使える幅の割合。右を先に確保して、左は残り
    constexpr CGFloat kRightMaxFraction = 0.5;
}

// バー全体。背景を設定の背景色で塗り(OSのテーマの色は使わない)、上端に境目の線を引いて、文字を描く。
@interface _MiataStatusBarView : NSView
@property (nonatomic, copy) NSString* leftText;
@property (nonatomic, copy) NSString* rightText;
@end
@implementation _MiataStatusBarView
- (BOOL)isFlipped { return YES; }
- (BOOL)isOpaque { return YES; }
// クリックは、このビューが受けて、何もしない(下の MiataRootView に落とさない。_MiataQueryBarView と同じ考え方)。
// first responder にならない(acceptsFirstResponder は既定の NO のまま)
- (NSView*)hitTest:(NSPoint)point
{
    return [super hitTest:point] ? self : nil;
}
// VoiceOver には、1 つの文字列として読ませる
- (BOOL)isAccessibilityElement { return YES; }
- (NSAccessibilityRole)accessibilityRole { return NSAccessibilityStaticTextRole; }
- (id)accessibilityValue
{
    if (self.leftText.length > 0 && self.rightText.length > 0) {
        return [NSString stringWithFormat:@"%@ %@", self.leftText, self.rightText];
    }
    return self.leftText.length > 0 ? self.leftText : (self.rightText ?: @"");
}
- (void)drawRect:(NSRect)dirtyRect
{
    // 背景と、上端の境目の線(入力バーと共通。dirtyRect を自分の範囲に切る理由も、そちらに書いてある)
    if (NSIsEmptyRect(miata::views::DrawBarBackground(self, dirtyRect))) return;

    NSColor* text_color = miata::views::NormalTextColor();
    using miata::views::kListPadding;
    NSMutableParagraphStyle* style = [[NSMutableParagraphStyle alloc] init];
    style.lineBreakMode = NSLineBreakByTruncatingTail;
    NSFont* font = miata::views::MakeFont(miata::Config::FontSize());
    NSDictionary* left_attrs = @{
        NSFontAttributeName: font,
        NSForegroundColorAttributeName: text_color,
        NSParagraphStyleAttributeName: style,
    };
    // 右(マークの要約)は、少し薄く(入力バーの件数と同じ)
    NSDictionary* right_attrs = @{
        NSFontAttributeName: font,
        NSForegroundColorAttributeName: [text_color colorWithAlphaComponent:miata::views::kBarSubTextAlpha],
        NSParagraphStyleAttributeName: style,
    };

    CGFloat width = self.bounds.size.width;
    CGFloat height = self.bounds.size.height;
    CGFloat inner = std::max((CGFloat)0, width - kListPadding * 2);

    // 縦位置は、ヘッダー(FileListView::DrawHeader)と同じ式(行の高さではなく、文字列の高さで中央に置く)
    NSString* left = self.leftText ?: @"";
    NSString* right = self.rightText ?: @"";

    CGFloat right_width = 0;
    if (right.length > 0) {
        const NSSize size = [right sizeWithAttributes:right_attrs];
        right_width = std::min(std::ceil(size.width), std::floor(inner * kRightMaxFraction));
        NSRect right_rect = NSMakeRect(width - kListPadding - right_width, (height - size.height) / 2, right_width, size.height);
        [right drawInRect:right_rect withAttributes:right_attrs];
    }
    if (left.length > 0) {
        NSSize size = [left sizeWithAttributes:left_attrs];
        CGFloat left_width = std::max((CGFloat)0, inner - (right_width > 0 ? right_width + kGap : 0));
        NSRect left_rect = NSMakeRect(kListPadding, (height - size.height) / 2, left_width, size.height);
        [left drawInRect:left_rect withAttributes:left_attrs];
    }
}
@end

namespace miata::views {

struct StatusBar::Impl {
    _MiataStatusBarView* view = nil;
    StatusText shown; // いま出している内容(同じ内容なら、AppKit に触らない)
};

StatusBar::StatusBar() : impl_(std::make_unique<Impl>())
{
    impl_->view = [[_MiataStatusBarView alloc] initWithFrame:NSMakeRect(0, 0, 200, Height())];
}

StatusBar::~StatusBar() = default;

void* StatusBar::NativeView() const
{
    return (__bridge void*)impl_->view;
}

double StatusBar::Height()
{
    return HeaderHeight();
}

void StatusBar::SetText(const StatusText& text)
{
    if (text == impl_->shown) return;
    impl_->shown = text;
    impl_->view.leftText = @(text.left.c_str());
    impl_->view.rightText = @(text.right.c_str());
    impl_->view.needsDisplay = YES;
}

const StatusText& StatusBar::Text() const
{
    return impl_->shown;
}

}

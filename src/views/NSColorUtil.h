#ifndef VIEWS_NS_COLOR_UTIL_H__
#define VIEWS_NS_COLOR_UTIL_H__

#import <AppKit/AppKit.h>
#include "../platform.h"
#include "../Config.h"

// AppKit依存のヘルパーなので.mmファイルからのみincludeする(Dialog.h/FileListView.h等の
// ビュー層ヘッダはAppKit非依存に保つ規約のため、ヘッダには置かない)。
namespace miata::views {
    inline NSColor* ToNSColor(const Color4f& c)
    {
        return [NSColor colorWithRed:c.r green:c.g blue:c.b alpha:c.a];
    }

    // 設定の文字の色(Miata.config.color.normal_text)。ヘッダー・入力バー・進捗パネルの文字
    inline NSColor* NormalTextColor()
    {
        return ToNSColor(Config::Color().Get(Config::Color::Type::NormalText));
    }

    // ステータスバー専用の文字の色(Miata.config.color.statusbar_text)。右側の文字と上端の線は、これから濃さを変えて導く
    inline NSColor* StatusBarTextColor()
    {
        return ToNSColor(Config::Color().Get(Config::Color::Type::StatusBarText));
    }

    // 入力バー(QueryBar)とステータスバー(StatusBar)の、文字の色に対する濃さ。上端の境目の線と、補助の文字(件数・マークの要約)
    inline constexpr CGFloat kBarBorderAlpha = 0.35;
    inline constexpr CGFloat kBarSubTextAlpha = 0.8;

    // 入力バーとステータスバーの背景: 背景色 background(OSのテーマの色は使わない。不透明で渡すこと)で塗り、上端に、
    // 一覧との境目の線(文字色 text の kBarBorderAlpha)を引く。色は呼ぶ側が決める。
    // dirtyRect は、自分の範囲(bounds)を超えて渡されることがある(親をまるごと描くとき。macOS 14以降、ビューは既定で
    // 自分の範囲に描画を切り詰めない)ので、そのまま塗ると、隣のビュー(一覧)を塗りつぶす。自分の範囲との交わりに切る。
    // 塗った範囲を返す(空なら、何も塗っていない)
    inline NSRect DrawBarBackground(NSView* view, NSRect dirty_rect, NSColor* background, NSColor* text)
    {
        const NSRect rect = NSIntersectionRect(dirty_rect, view.bounds);
        if (NSIsEmptyRect(rect)) return NSZeroRect;
        [background setFill];
        NSRectFill(rect);
        [[text colorWithAlphaComponent:kBarBorderAlpha] setFill];
        NSRectFillUsingOperation(NSIntersectionRect(NSMakeRect(0, 0, view.bounds.size.width, 1), rect), NSCompositingOperationSourceOver);
        return rect;
    }

    // 入力バー用: 設定の背景色(background)と文字色(normal_text)
    inline NSRect DrawBarBackground(NSView* view, NSRect dirty_rect)
    {
        return DrawBarBackground(view, dirty_rect, ToNSColor(Config::Background()), NormalTextColor());
    }

    // 設定の背景色(Config::Background)に合う見た目(暗い背景ならDark、明るければLight)。
    // ペインの境目の線やスクロールバーのように、OSが描く部品は、見た目(appearance)に合わせた色になる。
    // OSのテーマに任せると、暗い背景の上にLight用の部品(黒の薄い線=見えない、明るい帯)が載ってしまう。
    // ビュー単位で設定する(ウィンドウやアプリ全体には設定しない): pl_get_colorは描画の外ではビューの
    // appearanceに従わないので、全体を切り替えるとダイアログの背景色と中の部品の見た目が食い違う。
    inline NSAppearance* AppearanceForBackground()
    {
        auto bg = Config::Background();
        float luminance = 0.2126f * bg.r + 0.7152f * bg.g + 0.0722f * bg.b;
        return [NSAppearance appearanceNamed:luminance < 0.5f ? NSAppearanceNameDarkAqua : NSAppearanceNameAqua];
    }
}

#endif // VIEWS_NS_COLOR_UTIL_H__

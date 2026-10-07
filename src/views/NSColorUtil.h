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

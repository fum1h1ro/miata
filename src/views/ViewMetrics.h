#ifndef VIEWS_VIEW_METRICS_H__
#define VIEWS_VIEW_METRICS_H__

#import <AppKit/AppKit.h>
#include <cmath>
#include "../Config.h"

// 一覧(FileListView)と、各ペインの下の入力バー(QueryBar)・進捗パネル(ProgressOverlay)が共有する、フォントと寸法。
// AppKit依存のヘルパーなので.mmファイルからのみincludeする(NSColorUtil.hと同じ規約)。
namespace miata::views {
    // 一覧の行・ヘッダーと、入力バーの、左右の余白(pt)
    inline constexpr CGFloat kListPadding = 6;

    // Miata.config.set_font(name) で指定されたフォントを使う。未指定、または
    // 指定された名前が解決できない場合はシステムデフォルトフォントにフォールバックする。
    inline NSFont* MakeFont(CGFloat size)
    {
        auto& family = Config::FontFamily();
        if (!family.empty()) {
            NSFont* f = [NSFont fontWithName:@(family.c_str()) size:size];
            if (f) return f;
        }
        return [NSFont systemFontOfSize:size];
    }

    // 1 行の文字の高さ(フォントの上端から下端まで。行間を含む)
    inline CGFloat LineHeight(NSFont* font)
    {
        return std::ceil(font.ascender - font.descender + font.leading);
    }

    // ヘッダー(パス表示)の高さ。フォントサイズに応じて動的に決める(固定値のままだとフォントサイズを
    // 上げた時に文字が収まらなくなるため)。入力バーも同じ高さにそろえる。
    inline CGFloat HeaderHeight()
    {
        constexpr CGFloat kHeaderVerticalMargin = 13;
        return Config::FontSize() + 1 + kHeaderVerticalMargin;
    }
}

#endif // VIEWS_VIEW_METRICS_H__

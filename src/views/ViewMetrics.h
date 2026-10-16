#ifndef VIEWS_VIEW_METRICS_H__
#define VIEWS_VIEW_METRICS_H__

#import <AppKit/AppKit.h>
#include <cmath>
#include "../Config.h"

// 一覧(FileListView)・各ペインの下の入力バー(QueryBar)・進捗パネル(ProgressOverlay)・ステータスバー(StatusBar)・
// コンソール(ConsoleView)が共有する、フォントと寸法。
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

    // 1 行のバー(ヘッダー・入力バー・ステータスバー)の高さ。文字のサイズに応じて動的に決める(固定値のままだと
    // フォントサイズを上げた時に文字が収まらなくなるため)。文字のサイズが同じバーは、同じ高さになる。
    // (ProgressOverlay.mm の BarHeight は、進捗バーの太さで、別物)
    inline CGFloat SingleLineBarHeight(CGFloat font_size)
    {
        constexpr CGFloat kVerticalMargin = 13;
        return font_size + 1 + kVerticalMargin;
    }

    // ヘッダー(パス表示)の高さ(ファイル一覧のフォントサイズから決める)。入力バーも同じ高さにそろえる。
    // ステータスバーは、自分のフォントサイズ(Config::StatusBarFontSize)で SingleLineBarHeight を決める
    inline CGFloat HeaderHeight()
    {
        return SingleLineBarHeight(Config::FontSize());
    }

    // ファイル一覧の行の高さ。フォントサイズに応じて動的に決める(固定値のままだとフォントサイズを上げた時に行同士が重なってしまうため)。
    // 一覧とコンソールが共有する(コンソールの行が、一覧の行と同じ高さにそろう)。
    // (一覧の中の別の名前 RowHeight にしない: ProgressOverlay.mm の BarHeight の例のように、無名名前空間の同名の関数に隠される)
    inline constexpr CGFloat kRowVerticalMargin = 8; // 行の上下に確保する余白の合計
    inline CGFloat ListRowHeight()
    {
        return Config::FontSize() + kRowVerticalMargin;
    }
}

#endif // VIEWS_VIEW_METRICS_H__

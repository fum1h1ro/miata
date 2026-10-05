#ifndef VIEW_CONSTANTS_H__
#define VIEW_CONSTANTS_H__

#include <cstddef>
#include <cstdint>
#include <iterator>

namespace miata::views::constants {
    enum class Navigate : uint8_t {
        Up,
        Down,
        Left,
        Right,
        Ok,
        Cancel,
    };

    // 左右2ペインのどちらか。Luaには "left" / "right" の文字列で公開する
    enum class Pane : uint8_t {
        Left,
        Right,
    };

    // ペインごとの入力(ペインの下端に出すバー)の種類。バーはペインごと・種類ごとに1つ。1ペインの中では、
    // ペインの下端から、kQueryKindsの順に積む(先頭が一番下)。
    enum class QueryKind : uint8_t {
        Search, // ファイル名の検索(vimの / )。一番下
        Filter, // 絞り込み(名前に語を含む行だけを出す)。検索の上
    };
    inline constexpr QueryKind kQueryKinds[] = {QueryKind::Search, QueryKind::Filter};
    inline constexpr size_t kQueryKindCount = std::size(kQueryKinds);

    // Quick Lookのプレビューを一覧の上に被せる範囲。Luaには "both" / "left" / "right" の文字列で公開する
    enum class QuickLookArea : uint8_t {
        Both, // 両ペインにまたがる1枚
        Left,
        Right,
    };
}




#endif // VIEW_CONSTANTS_H__

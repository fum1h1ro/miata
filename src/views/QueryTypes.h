#ifndef VIEWS_QUERY_TYPES_H__
#define VIEWS_QUERY_TYPES_H__

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>

namespace miata::views {
    // ペインごとの入力(ファイル名の検索 = SearchState と、絞り込み = FilterState)が共有する値の型。
    // AppKitに依存しない。

    // 名前(UTF-16)の中の、語に一致した範囲。NSRangeと同じ単位だが、AppKit型はヘッダに持ち込まない
    // 規約なので自前で持つ。
    struct MatchRange {
        size_t location = 0;
        size_t length = 0;
    };

    // 入力の状態
    enum class QueryMode : uint8_t {
        Idle,      // 入力も、確定した語も無い
        Typing,    // 語を入力中(打つたびに、カーソルが動く / 一覧が絞られる)
        Committed, // 確定済み
    };

    // 一覧の中の位置。再スキャンで一覧の並びが変わっても同じファイルを指せるように、パスで持つ
    // (FileListView::CursorMemoと同じ考え方)。rowは、そのパスが一覧に見つからないときの代わり。
    // rowが何の添字かは、使う側が決める: 検索は表示順(list_)、絞り込みは絞り込む前の全体の並び(sorted_)。
    struct ListPosition {
        std::optional<std::filesystem::path> path; // 一覧が空なら無い
        int row = 0;
    };
}

#endif // VIEWS_QUERY_TYPES_H__

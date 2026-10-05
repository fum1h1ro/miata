#ifndef VIEWS_NAME_MATCHER_H__
#define VIEWS_NAME_MATCHER_H__

#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "QueryTypes.h"

namespace miata::views {
    // 名前に一致した箇所(すべての出現)。範囲は名前(UTF-16)の添字で、そのまま属性文字列の範囲に使える
    struct NameMatch {
        std::vector<MatchRange> ranges; // 左から、重ならない。1つ以上ある
    };

    // 語(検索語・絞り込みの語)がファイル名に一致するかを調べる。検索(FileListView::RebuildSearchHits)と
    // 絞り込み(FileListView::ApplyFilter)が共用する。語は作るときに1回だけ解釈する(名前ごとに解釈し直さない)。
    //
    // いまは部分一致だけ: 語を部分文字列として含む名前に一致する。語に大文字(Unicodeの大文字を含む)が1文字でも
    // あれば大文字小文字を区別し、無ければ区別しない(スマートケース)。合成済みの文字と分解された文字
    // (「が」と「か」+濁点)は同じ文字として一致する。全角/半角・ひらがな/カタカナ・アクセントは畳み込まない。
    // あいまい一致(fzf風)は、一致位置を返せる自前のアルゴリズムを足して、ここに閉じ込める
    // (外部のfzfは一致位置を返さないので、ハイライトに使えない)。
    class NameMatcher {
    public:
        explicit NameMatcher(const std::string& query);
        ~NameMatcher();
        NameMatcher(const NameMatcher&) = delete;
        NameMatcher& operator=(const NameMatcher&) = delete;

        // 語が空、またはNSStringにしたとき空になる(UTF-8として不正、BOMだけなど)とき真。真のときは、どの名前にも
        // 一致しない(Match()はnullopt)。それをどう扱うかは呼ぶ側が決める: 絞り込みは「絞り込みなし」(全行)、
        // 検索は「ヒット無し」にする
        bool Empty() const;
        // nameの中の、語の出現箇所をすべて見つける。一致しなければnullopt。nameはUTF-8
        std::optional<NameMatch> Match(const std::string& name) const;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}

#endif // VIEWS_NAME_MATCHER_H__

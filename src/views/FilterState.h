#ifndef VIEWS_FILTER_STATE_H__
#define VIEWS_FILTER_STATE_H__

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "QueryTypes.h"

namespace miata::views {
    // 絞り込みバーに出す内容
    struct FilterStatus {
        QueryMode mode = QueryMode::Idle;
        MatchKind kind = MatchKind::Substring; // 一致のしかた。Idleは部分一致
        std::string query;     // 語。入力中は入力中の語、確定後は確定した語。Idleは空
        int shown = 0;         // 見えている行の数(語が空なら、全行)
        int total = 0;         // 絞り込む前の全行の数
        int hidden_marks = 0;  // 絞り込みで隠れている行の、マーク済みの数
        bool fuzzy_fallback = false; // あいまい一致のはずが、fzfを使えず、部分一致で絞り込んでいる
    };

    // 一致のしかたのLuaでの名前("substring" / "fuzzy")。名前から引くときは、知らない名前ならnullopt
    const char* MatchKindName(MatchKind kind);
    std::optional<MatchKind> ParseMatchKind(std::string_view name);

    // 絞り込みバーの右端に出す文言。絞り込んでいない(Idle、または語が空)なら出さない(空の文字列)。
    // 一致が無ければ「見つかりません」、あれば「見えている行数/全行数」。隠れているマークがあれば、続けて
    // 「  隠れたマーク N」を、あいまい一致がfzfなしの部分一致になっていれば、末尾に「  fzfなし(部分一致)」を足す。
    // AppKitに依存しない純関数(バーの表示は、この文字列を出すだけ)。
    std::string FilterCountText(const FilterStatus& status);

    // 絞り込みで語が変わった後、カーソルをどの行に置くか。row_of_sortedは、絞り込む前の全体の並び(sorted_)の
    // 各要素が、絞り込み後の一覧の何行目か(隠れていれば-1)。pivotは、カーソルを置きたかった要素の、全体の並びでの添字。
    // pivot以降で最初に見える要素の行 → 無ければ、手前で最後に見える要素の行 → 何も見えなければ0。
    // (見えている要素がpivotなら、その行。カーソルのファイルが残っていれば、そのファイルに留まる)
    // pivotが範囲外なら丸める。並びが空なら0
    int NearestShownRow(const std::vector<int>& row_of_sorted, int pivot);

    // 1ペイン分の絞り込みの状態。AppKitに依存せず、遷移(Idle → Typing → Committed)と、基準の位置、一致箇所の
    // 保持だけを持つ。実際の一致の計算(NameMatcher)と、一覧の作り直し・カーソルの移動・描画はFileListViewが行う。
    //
    // 検索(SearchState)との違い:
    //  - 語が空のときは「絞り込みなし」(全行を出す)。入力を始めた直後は語が空なので、前の絞り込みは外れて全行になる
    //    (画面に見えている状態と、そのままEnterで確定される状態が、常に一致する)。取っておいた語は、Escで戻る
    //  - 語が空のまま確定(Enter)すると、検索のように「始める前の状態に戻る」のではなく、絞り込みを解除する
    //  - ヒットの行ごとの一覧(SearchHit)ではなく、見えている行(list_)ごとの一致箇所を持つ(あいまい一致は、一致箇所を持たない)
    //  - 語の一致のしかた(MatchKind)を持つ。入力を始めるときに決まり、確定済みの絞り込みを取っておくときは、語と一緒に取っておく
    //
    // 基準(Anchor)と戻り先(Origin)は、絞り込む前の全体の並び(sorted_)での位置(パスと添字。ListPosition)。
    // 絞り込みで行が入れ替わっても、同じファイルを指せるように、画面の行(list_)の添字ではなく、全体の並びで持つ。
    class FilterState {
    public:
        QueryMode Mode() const { return mode_; }
        bool Active() const { return mode_ != QueryMode::Idle; }
        // 画面に出す語。入力中は入力中の語、確定後は確定した語。Idleは空。語が空なら、絞り込みなし(全行)
        const std::string& Query() const { return query_; }
        // 語の一致のしかた。Idleは部分一致
        MatchKind Kind() const { return kind_; }

        // --- 遷移 ---
        // 入力を始める(Idle / Committed → Typing)。originは、始めたときのカーソルの位置で、入力中にEscを押したときに
        // 戻る先(Origin)であり、最初の基準(Anchor)でもある。確定済みの絞り込みがあれば、Escで戻れるよう語(と一致のしかた)を
        // 取っておく。新しい語はまだ空(=絞り込みなし)。kindは、新しい語の一致のしかた。すでにTypingなら何もしない。
        void Begin(ListPosition origin, MatchKind kind);
        // 入力中の語を更新する(Typingのときだけ)。一致箇所は、SetRowRanges()で別に渡す。
        void SetQuery(std::string query);
        // 入力を確定する(Typing → Committed)。語が空なら、確定ではなく解除する(Idle)。
        void Commit();
        // 入力を取り消す(Typing → 始める前の状態)。取っておいた語があればその語(と一致のしかた)で確定済み(Committed)、
        // 無ければIdle。
        // 基準(Anchor)は、戻り先(Origin)に戻す。カーソルを戻すのは呼び出し側(Origin()。Cancel()の後も読める)。
        void Cancel();
        // 入力せずに、語を確定済みにする(Typing以外から)。語が空なら解除する(Clear)。anchorは、基準にも戻り先にもなる
        // (0件になったあとの解除で、位置を失わないため)。
        void Set(std::string query, ListPosition anchor, MatchKind kind);
        // 解除する(どの状態からでもIdleへ)。基準も戻り先も消える。
        void Clear();

        // --- 基準 ---
        // 入力中に、カーソルを置きたいファイルの位置。語を打つたびに、一覧が絞り込まれて行が入れ替わるので、
        // 画面のカーソル(行の添字)ではなく、これを基準にして、見える行へ寄せる。0件になっても失わない(語を戻せば、
        // 元の位置に戻る)。↓ / ↑でカーソルを動かしたときは、動いた先を新しい基準にする(SetAnchor)。
        const ListPosition& Anchor() const { return anchor_; }
        void SetAnchor(ListPosition anchor) { anchor_ = std::move(anchor); }
        // 入力を始めたときのカーソルの位置。入力中にEscを押したとき、カーソルを戻す先
        const ListPosition& Origin() const { return origin_; }

        // --- 一致箇所(ハイライト) ---
        // 見えている行(list_)ごとの一致箇所。list_と同じ並び・同じ長さ。語が空(絞り込みなし)のときは空(強調なし)。
        void SetRowRanges(std::vector<std::vector<MatchRange>> ranges) { row_ranges_ = std::move(ranges); }
        // rowの行の一致箇所。範囲外、または一致箇所が無ければnullptr
        const std::vector<MatchRange>* RangesFor(int row) const;

    private:
        // 入力を始める前に確定していた絞り込み
        struct Saved {
            std::string query;
            MatchKind kind;
        };

        QueryMode mode_ = QueryMode::Idle;
        MatchKind kind_ = MatchKind::Substring;
        std::string query_;
        std::optional<Saved> saved_; // Typingの間だけ。入力を始める前に確定していた絞り込み(無ければnullopt)
        ListPosition anchor_;
        ListPosition origin_;
        std::vector<std::vector<MatchRange>> row_ranges_;
    };
}

#endif // VIEWS_FILTER_STATE_H__

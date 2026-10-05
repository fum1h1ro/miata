#ifndef VIEWS_SEARCH_STATE_H__
#define VIEWS_SEARCH_STATE_H__

#include <optional>
#include <string>
#include <utility>
#include <vector>
#include "QueryTypes.h"

namespace miata::views {
    // 一致した行(表示順の添字)と、その名前の中の一致箇所(すべての出現。左から、重ならない)
    struct SearchHit {
        int row = 0;
        std::vector<MatchRange> ranges;
    };

    // 検索バーに出す内容
    struct SearchStatus {
        QueryMode mode = QueryMode::Idle;
        std::string query;
        int hit_count = 0;
        int ordinal = 0; // カーソルがマッチの行にあるとき、その順番(1始まり)。そうでなければ0
    };

    // 検索バーの右端に出す件数の文言。検索していない、または語が空なら出さない(空の文字列)。
    // マッチが無ければ「見つかりません」。あれば「順番/件数」(カーソルがマッチの行にいなければ「-/件数」)。
    // AppKitに依存しない純関数(バーの表示は、この文字列を出すだけ)。
    std::string SearchCountText(const SearchStatus& status);

    // 1ペイン分の検索の状態。AppKitに依存せず、遷移(Idle → Typing → Committed)と、ヒットからの
    // 計算(n / N、件数)だけを持つ。実際のマッチ(名前に対する検索)と、カーソルの移動・描画は
    // FileListViewが行い、結果をSetHits()で渡す。
    //
    // ヒット(SearchHit::row)は表示順(FileListViewのlist_)の添字なので、一覧が作り直されるたび
    // (FileListView::ApplyFilter)に無効になる。FileEntryViewのポインタのように寿命が切れる訳ではないが、
    // 指す行が変わるので、作り直すたびにSetHits()で渡し直すこと。語が変わる遷移(Begin / Cancel / Clear、
    // それと、語が空のCommit)はヒットを空にするので、遷移の後に今の語のヒットが要るなら、SetHits()で
    // 渡し直す(語がある普通のCommitは、語もヒットも変わらない)。
    class SearchState {
    public:
        QueryMode Mode() const { return mode_; }
        bool Active() const { return mode_ != QueryMode::Idle; }
        // 画面に出す語。入力中は入力中の語、確定後は確定した語。Idleは空
        const std::string& Query() const { return query_; }

        // --- 遷移 ---
        // 入力を始める(Idle / Committed → Typing)。originは、始めたときのカーソルの位置。入力中に
        // Escを押したときに戻る先(Origin)であり、最初の起点(Anchor)でもある。確定済みの検索があれば、
        // Escで戻れるよう語を取っておく。新しい語はまだ空で、前の検索のヒットは隠す。
        // すでにTypingなら何もしない。
        void Begin(ListPosition origin);
        // 入力中の語を更新する(Typingのときだけ)。ヒットはSetHits()で別に渡す。
        void SetQuery(std::string query);
        // 入力を確定する(Typing → Committed)。語が空なら、検索を始める前の状態に戻る
        // (確定済みの検索があればそれ、無ければIdle)。
        void Commit();
        // 入力を取り消す(Typing → 検索を始める前の状態)。カーソルを戻すのは呼び出し側(Origin()。
        // Cancel()の後も読める。消えるのはClear()のときだけ)。
        void Cancel();
        // 検索を終える(どの状態からでもIdleへ)。
        void Clear();

        // --- 起点 ---
        // 入力中にマッチを探し始める位置。入力するたびに、ここから前方(末尾まで行ったら先頭に戻る)の
        // 最初のマッチへ動く。↓ / ↑でマッチを移ったときは、移った先に動かす(以降の入力はそこから探す)。
        // 語を全部消したときは、Origin()に戻す(検索を始めた位置からやり直す)。
        const ListPosition& Anchor() const { return anchor_; }
        void SetAnchor(ListPosition anchor) { anchor_ = std::move(anchor); }
        // 検索を始めたときのカーソルの位置。入力中にEscを押したとき、カーソルを戻す先
        const ListPosition& Origin() const { return origin_; }

        // --- ヒット ---
        // rowの昇順であること。
        void SetHits(std::vector<SearchHit> hits) { hits_ = std::move(hits); }
        int HitCount() const { return static_cast<int>(hits_.size()); }
        // rowの行の一致箇所。マッチの行でなければnullptr。
        const std::vector<MatchRange>* RangesFor(int row) const;
        // row以降(rowを含む)の最初のマッチの行。末尾までに無ければ先頭のマッチ(ラップ)。ヒットが無ければnullopt
        std::optional<int> FirstHitFrom(int row) const;
        // rowの次(dir > 0)・前(dir < 0)のマッチの行。rowがマッチの行でも、そうでなくても、同じ式で求める
        // (rowそのものは含まない)。端でラップする。ヒットが無ければnullopt
        std::optional<int> Step(int row, int dir) const;
        // rowがマッチの行なら、その順番(1始まり)。そうでなければ0
        int Ordinal(int row) const;

    private:
        std::vector<SearchHit>::const_iterator LowerBound(int row) const;

        QueryMode mode_ = QueryMode::Idle;
        std::string query_;
        std::optional<std::string> saved_query_; // Typingの間だけ。入力を始める前に確定していた語(無ければnullopt)
        ListPosition anchor_;
        ListPosition origin_;
        std::vector<SearchHit> hits_;
    };
}

#endif // VIEWS_SEARCH_STATE_H__

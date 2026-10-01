#ifndef VIEWS_SEARCH_STATE_H__
#define VIEWS_SEARCH_STATE_H__

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace miata::views {
    // 名前(UTF-16)の中の、検索語に一致した範囲。NSRangeと同じ単位だが、AppKit型はヘッダに持ち込まない
    // 規約なので自前で持つ。
    struct SearchRange {
        size_t location = 0;
        size_t length = 0;
    };

    // 一致した行(表示順の添字)と、その名前の中の一致箇所(すべての出現。左から、重ならない)
    struct SearchHit {
        int row = 0;
        std::vector<SearchRange> ranges;
    };

    enum class SearchMode : uint8_t {
        Idle,      // 検索していない
        Typing,    // 検索語を入力中(打つたびに、カーソルがマッチへ動く)
        Committed, // 確定済み(n / N で次・前のマッチへ動く)
    };

    // 検索バーに出す内容
    struct SearchStatus {
        SearchMode mode = SearchMode::Idle;
        std::string query;
        int hit_count = 0;
        int ordinal = 0; // カーソルがマッチの行にあるとき、その順番(1始まり)。そうでなければ0
    };

    // 一覧の中の位置。再スキャンで一覧の並びが変わっても同じファイルを指せるように、パスで持つ
    // (FileListView::CursorMemoと同じ考え方)。rowは、そのパスが一覧に見つからないときの代わり。
    struct SearchPosition {
        std::optional<std::filesystem::path> path; // 一覧が空なら無い
        int row = 0;
    };

    // 1ペイン分の検索の状態。AppKitに依存せず、遷移(Idle → Typing → Committed)と、ヒットからの
    // 計算(n / N、件数)だけを持つ。実際のマッチ(名前に対する検索)と、カーソルの移動・描画は
    // FileListViewが行い、結果をSetHits()で渡す。
    //
    // ヒット(SearchHit::row)は表示順(FileListViewのlist_)の添字なので、一覧が作り直されるたび
    // (FileListView::Fetch)に無効になる。FileEntryViewのポインタのように寿命が切れる訳ではないが、
    // 指す行が変わるので、作り直すたびにSetHits()で渡し直すこと。語が変わる遷移(Begin / Cancel / Clear、
    // それと、語が空のCommit)はヒットを空にするので、遷移の後に今の語のヒットが要るなら、SetHits()で
    // 渡し直す(語がある普通のCommitは、語もヒットも変わらない)。
    class SearchState {
    public:
        SearchMode Mode() const { return mode_; }
        bool Active() const { return mode_ != SearchMode::Idle; }
        // 画面に出す語。入力中は入力中の語、確定後は確定した語。Idleは空
        const std::string& Query() const { return query_; }

        // --- 遷移 ---
        // 入力を始める(Idle / Committed → Typing)。originは、始めたときのカーソルの位置。入力中に
        // Escを押したときに戻る先(Origin)であり、最初の起点(Anchor)でもある。確定済みの検索があれば、
        // Escで戻れるよう語を取っておく。新しい語はまだ空で、前の検索のヒットは隠す。
        // すでにTypingなら何もしない。
        void Begin(SearchPosition origin);
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
        const SearchPosition& Anchor() const { return anchor_; }
        void SetAnchor(SearchPosition anchor) { anchor_ = std::move(anchor); }
        // 検索を始めたときのカーソルの位置。入力中にEscを押したとき、カーソルを戻す先
        const SearchPosition& Origin() const { return origin_; }

        // --- ヒット ---
        // rowの昇順であること。
        void SetHits(std::vector<SearchHit> hits) { hits_ = std::move(hits); }
        int HitCount() const { return static_cast<int>(hits_.size()); }
        // rowの行の一致箇所。マッチの行でなければnullptr。
        const std::vector<SearchRange>* RangesFor(int row) const;
        // row以降(rowを含む)の最初のマッチの行。末尾までに無ければ先頭のマッチ(ラップ)。ヒットが無ければnullopt
        std::optional<int> FirstHitFrom(int row) const;
        // rowの次(dir > 0)・前(dir < 0)のマッチの行。rowがマッチの行でも、そうでなくても、同じ式で求める
        // (rowそのものは含まない)。端でラップする。ヒットが無ければnullopt
        std::optional<int> Step(int row, int dir) const;
        // rowがマッチの行なら、その順番(1始まり)。そうでなければ0
        int Ordinal(int row) const;

    private:
        std::vector<SearchHit>::const_iterator LowerBound(int row) const;

        SearchMode mode_ = SearchMode::Idle;
        std::string query_;
        std::optional<std::string> saved_query_; // Typingの間だけ。入力を始める前に確定していた語(無ければnullopt)
        SearchPosition anchor_;
        SearchPosition origin_;
        std::vector<SearchHit> hits_;
    };
}

#endif // VIEWS_SEARCH_STATE_H__

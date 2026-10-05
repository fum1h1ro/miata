#ifndef VIEWS_FILE_LIST_VIEW_H__
#define VIEWS_FILE_LIST_VIEW_H__

#include <chrono>
#include <expected>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include "../models/Model.h"
#include "../misc.h"
#include "../platform.h"
#include "FilterState.h"
#include "SearchState.h"

namespace miata::views {
    class FileEntryView {
    public:
        FileEntryView(models::FileEntryModel& entry)
        {
            entry_model_ = &entry;
        }

        inline models::FileEntryModel& Model()
        {
            return *entry_model_;
        }
    private:
        models::FileEntryModel* entry_model_;
    };

    // ファイル一覧1ペイン分。実体はNSScrollView+自前NSView(Core Text描画)で、
    // AppKit型はFileListView.mmに閉じ込める。
    class FileListView {
    public:
        enum class SortKey {
            Name,
            Size,
            ModifiedTime,
            Extension,
        };

        FileListView(models::FileListModel& list);
        ~FileListView();

        // 親(BrowserView)にaddSubviewするためのNSView*を(__bridge void*)で返す
        void* NativeView() const;
        // それぞれ対応するNSViewのdrawRect:から呼ばれる。呼び出し元のビュー自身の
        // 座標系で描画するため、ヘッダーとリスト本体でメソッドを分けている。
        // Draw()のmin_y〜max_yは、描き直す範囲(dirtyRectのy)。その範囲にかかる行だけを描く
        // (数千〜数万件のディレクトリでも、見えている行数分のコストで済むように)。
        void DrawHeader();
        void Draw(double min_y, double max_y);

        void SetSort(SortKey key, bool reverse);
        SortKey GetSortKey() const { return sort_key_; }
        bool GetSortReverse() const { return sort_reverse_; }
        // ソートの基準の名前("name" / "size" / "mtime" / "ext")との変換。Luaのsortと、ペインの状態の保存(PaneState)で
        // 同じ名前を使う。名前が違えばParseSortKeyはnullopt。
        static std::optional<SortKey> ParseSortKey(std::string_view name);
        static const char* SortKeyName(SortKey key);

        bool GetFocus() const { return focus_; }
        void SetFocus(bool focus);
        int GetCursor() const { return cursorIndex_; }
        void SetCursor(int index);
        void MoveCursor(int offset);
        // カーソル下のエントリ。一覧が空(カーソルが一覧の外)ならnullptr
        inline FileEntryView* CurrentOrNull() const
        {
            if (cursorIndex_ < 0 || (size_t)cursorIndex_ >= list_.size()) return nullptr;
            return list_[(size_t)cursorIndex_];
        }
        // カーソル下のファイルのパス。一覧が空ならnullopt
        std::optional<std::filesystem::path> CurrentPath() const;
        // このペインの、見えているマーク済みエントリ(絞り込みで隠れている行のマークは含まない)を、画面表示順
        // (list_の順。モデルの走査順ではない)で返す。マークが無ければ空。ポインタは再スキャン(Fetch)で無効になるので、
        // 返った直後に使い、保持しない。ファイル操作(コピー・移動・ゴミ箱・リネーム)の対象と、ドラッグ(BeginDrag)、
        // Luaのmarked_entriesが使う
        std::vector<models::FileEntryModel*> MarkedEntries() const;

        // カーソル移動やフォーカス変更を伴わない外部要因(マーク変更等)の後に呼ぶ再描画要求
        void Redraw();

        // pathsのファイルのマークを外して、再描画を要求する(画面に出ていない=絞り込みで隠れているファイルも含めて、
        // モデルのマークを外す)。モデルのMark()はフラグを書き換えるだけでビューには通知されないので、画面に反映したいときは
        // こちらを使う。ファイル操作の完了後やドロップ後に、操作したファイルのマークだけを外すために使う
        // (そのペインの全マークを外すと、操作の最中に付けたマークや、絞り込みで隠れているマークまで消える)。
        void UnmarkPaths(const std::vector<std::filesystem::path>& paths);

        // --- マークの一括操作(Luaのmark_all / unmark_all / invert_marks / mark_range / mark_search_hits / next_mark / prev_mark) ---
        // 対象は、画面に出ている行(list_)。絞り込み中は、絞り込んだ後の行だけが対象になる(絞り込んだ上で全マーク、ができる。
        // 隠れた行のマークは、変えない)。マークを変える操作は、マークの状態が変わったときだけ、再描画を要求する(1回)。
        // ここではダイアログの表示中かどうかを見ない(閉じた直後のティックにも呼ばれる。jump_toと同じ)。
        enum class MarkMode {
            Mark,   // マークする
            Unmark, // マークを外す
            Toggle, // 反転する
        };
        // 対象にするエントリの種類。フォルダかどうかはIsDirectory()(シンボリックリンクはたどる。Luaのis_dirと同じ。
        // 壊れたリンクはフォルダではない)
        enum class MarkKind {
            All,
            Files,
            Dirs,
        };
        // 範囲マークで、起点にするマークを、カーソルのどちら側に探すか
        enum class MarkRangeFrom {
            Above, // カーソルより上(表示順で前)
            Below, // カーソルより下(表示順で後)
        };
        struct MarkResult {
            int matched = 0; // 対象になった行の数
            int changed = 0; // そのうち、マークの状態が実際に変わった数
        };
        // kindに合う全ての行に、modeを適用する
        MarkResult MarkAll(MarkMode mode, MarkKind kind);
        // 起点(カーソルの行を除いて、fromの側でいちばん近いマーク済みの行)から、カーソルの行まで、両端を含む全ての行を
        // マークする(カーソルの行がマーク済みでも、起点にはしない)。起点が無ければ、何もしない({0, 0})。
        // カーソルは動かさず、kindは見ずに、フォルダも含める。
        MarkResult MarkRange(MarkRangeFrom from);
        // いまの検索(入力中も確定後も)のヒットのうち、kindに合う行に、modeを適用する。検索していなければ({0, 0})。
        // 検索は終わらせない
        MarkResult MarkSearchHits(MarkMode mode, MarkKind kind);
        // カーソルを、次(dir > 0)・前(dir <= 0)のマーク済みの行へ動かす(端でラップ。カーソルの行そのものは含まない)。
        // マーク済みの行が(カーソルの行以外に)無くて動けなければfalse
        bool StepMark(int dir);

        // このペインのディレクトリを再スキャンする(FileListModel::Reload()参照)。
        // マークもカーソルも、パスで同じファイルを引き継ぐ。カーソルのファイルが消えていた
        // 場合は、再スキャン前の画面上の並びで次に残っているファイル(無ければその前)へ寄せる。
        // cursor_toを渡すと、再スキャン後にそのパスのファイルへカーソルを合わせる(リネームの
        // 直後など、旧パスが消えて新しいパスに移るとき用。一覧に無ければ上記の通常の寄せ方)。
        // 失敗時(ディレクトリが読めない等)は何も変えずにエラーを返す(権限が無い失敗は、
        // FileError::permission_deniedで分かる)。
        std::expected<void, FileError> Reload(std::optional<std::filesystem::path> cursor_to = std::nullopt);

        // --- マーク済みファイルのドラッグ&ドロップ(他アプリへの持ち出し) ---
        // 実際のドラッグ開始とドラッグ画像はFileListView.mm内のNSViewが担い、ここは
        // 「何を運ぶか」と「ドロップ後にどうするか」だけを決める。

        // ドラッグ開始を許可するかを上位(View)が判断するためのコールバック
        // (ダイアログ表示中は無効にする用途)。未設定なら常に許可する。
        void SetDragGuard(std::function<bool()> guard);

        struct DragEntry {
            std::filesystem::path path;
            bool is_directory;
        };
        // ドラッグ開始時に呼ぶ。このペインの、見えているマーク済みエントリ(絞り込みで隠れている行のマークは含まない)を
        // 画面表示順で返す。
        // どの行を押してドラッグしたかは問わない。マークが無い、またはDragGuardが
        // 拒否した場合は空を返す(=ドラッグしない)。
        const std::vector<DragEntry>& BeginDrag();
        // ドラッグ終了時に呼ぶ。acceptedは宛先がドロップを受理したか(キャンセル/拒否ならfalse)。
        // 受理された場合、ファイルが移動されていればReload()で一覧を最新にし(カーソルは維持、
        // 移されなかったファイルのマークは残る)、そうでなければ、運んだファイルのマークだけ解除する。
        void EndDrag(bool accepted);

        // --- ディレクトリ監視による自動リロード ---
        // 表示中のディレクトリを監視し、変化(直下のエントリの追加・削除・改名、既存ファイルの中身・
        // 更新日時の更新)を検知したら、少し待ってからReload()する(カーソルとマークは維持される)。
        // 短時間に続く変化は1回にまとめ、走査が重いディレクトリでは間隔をあける。監視できない場所
        // (ネットワークボリュームの他のマシンからの変更など)は、手動のReload()で反映する。
        // 毎ティック(Application::Update)から呼ぶこと。allowがfalseの間は、検知していても保留して
        // 許可された最初のティックで反映する(ダイアログ表示中は、リネームの入力中にカーソルが動いて
        // 別のファイルを改名してしまうのを避けるため保留する)。
        void UpdateAutoReload(bool allow);

        // --- 検索(vimの / のようなインクリメンタル検索) ---
        // 検索はペインごと。状態の遷移とヒットからの計算はSearchState、ここは実際のマッチ(名前に対する検索)と、
        // カーソルの移動、ハイライトの描画を受け持つ。マッチは、検索語を部分文字列として含む名前(検索語に大文字が
        // 無ければ大文字小文字を区別しない=スマートケース)。名前の中のすべての出現を強調し、カーソルのある行の
        // 一致部分は別の色にする。一覧が作り直されたとき(リロード・ソート)は、検索語を保ったままヒットを
        // 再計算し、ディレクトリを移動したときは検索を消す。ペインの下の検索バーは、BrowserViewが持つ。

        // 検索語の入力を始める(Idle / Committed → Typing)。入力するまでヒットは無い(前の検索のハイライトは隠れる)。
        // すでにTypingなら何もしない。
        void BeginSearch();
        // 入力中の語を更新する(Typingのときだけ)。検索を始めた位置(↓ / ↑で動かしていれば、その位置)から
        // 前方(末尾まで行ったら先頭に戻る)の最初のマッチ(その位置自身を含む)へ、カーソルを動かす。
        // マッチが無ければ(語が空のときも)その位置に戻す。語を全部消したときは、↓ / ↑で動かしていても、
        // 検索を始めた位置からやり直す。
        void SetSearchQuery(const std::string& query);
        // 次(dir > 0)・前(dir < 0)のマッチのファイルへカーソルを動かす(端でラップ)。入力中は、動いた先を
        // 以降の入力の起点にする。検索していない、またはマッチが無くて動けなければfalse。
        bool StepSearch(int dir);
        // 入力を確定する(Typing → Committed)。カーソルは動かさない。語が空なら、検索を始める前の状態
        // (確定済みの検索があればそれ、無ければ検索なし)に戻る。
        void CommitSearch();
        // 入力を取り消す(Typing → 検索を始める前の状態)。カーソルは、検索を始めた位置に戻す。
        void CancelSearch();
        // 検索を終える(ハイライトを消す)。カーソルは動かさない。検索していたらtrue。
        bool ClearSearch();
        QueryMode GetSearchMode() const { return search_.Mode(); }
        // 検索バーに出す内容(語、ヒット数、カーソルのある行の順番)
        SearchStatus GetSearchStatus() const;

        // --- 絞り込み(語を部分文字列として含む名前の行だけを、一覧に出す) ---
        // 絞り込みはペインごと。状態の遷移はFilterState、ここは実際の一致(NameMatcher)と、一覧(list_)の作り直し、
        // カーソルの追従、ハイライトの描画を受け持つ。語に大文字が無ければ大文字小文字を区別しない(スマートケース。検索と同じ)。
        // 一致した部分(すべての出現)は、検索とは別の色(filter_match)で強調する。list_は、絞り込んだ後の行だけになる。
        // 絞り込む前の全エントリのソート済みの並びはsorted_に持ち、語が変わるたびに、再ソートせずに、そこから選び直す。
        // 検索(/ n N)、マークの一括操作、ドラッグ、Quick Look、Luaのcursor_entry / marked_entriesは、list_を見るので、
        // 見えている行だけが対象になる。再スキャン(リロード・ソート)では絞り込みを保ち、ディレクトリを移動したら解除する。
        // ペインの下の入力バーは、BrowserViewが持つ。

        // 語の入力を始める(Idle / Committed → Typing)。語はまだ空なので、全行が出る(前の絞り込みは外れる。Escで戻る)。
        // カーソルは、同じファイルに留まる。すでにTypingなら何もしない。
        void BeginFilter();
        // 入力中の語を更新する(Typingのときだけ)。一覧を絞り込み直し、カーソルを、基準のファイルへ寄せる(そのファイルが
        // 隠れたら、一覧の並びで次に見えるファイル、無ければその前)。0件になっても基準は失わない(語を戻せば、元に戻る)。
        void SetFilterQuery(const std::string& query);
        // 一覧のカーソルを、1行下(dir > 0)・上(dir < 0)へ動かす(端で止まる。折り返さない)。動いた先を、以降の入力の
        // 基準にする。入力中でない、または動けなければfalse。
        bool StepFilter(int dir);
        // 入力を確定する(Typing → Committed)。語が空なら、確定ではなく解除する。
        void CommitFilter();
        // 入力を取り消す(Typing → 始める前の状態)。始める前に確定していた絞り込みがあれば、それに戻る。
        // カーソルは、入力を始めたときのファイルへ戻す。
        void CancelFilter();
        // 絞り込みを解除する(どの状態からでも)。カーソルは、今のファイルに留まる(0件だったときは、基準のファイル)。
        // 絞り込んでいたらtrue。
        bool ClearFilter();
        // 入力欄を使わずに、語を確定済みにする(Luaのfilter_set)。語が空なら解除する。入力中でないときに呼ぶこと。
        // 戻り値は、結果の状態(見えている行数と全行数)。
        FilterStatus SetFilter(const std::string& query);
        QueryMode GetFilterMode() const { return filter_.Mode(); }
        // 入力中の語、または確定した語(絞り込んでいなければ空)
        const std::string& FilterQuery() const { return filter_.Query(); }
        // 絞り込みバーに出す内容(語、見えている行数と全行数、隠れているマークの数)
        FilterStatus GetFilterStatus() const;
        // 絞り込みで隠れている行の、マーク済みの数(絞り込んでいなければ0)
        int HiddenMarkCount() const;

        // 一覧の下端に空ける高さ(pt)。一覧(スクロール部分)がこの分だけ縮む。空いた帯には、親(BrowserView)が
        // このペインの入力バー(検索・絞り込み)を重ねる(反対側のペインは縮まない)。0で空けない。縮んでカーソルが
        // 見えなくなるときは、見える位置までスクロールする。
        void SetBottomInset(double height);

    private:
        void Fetch();

        // list_の行のうち、in_scope(行の添字)が真で、kindに合う行に、modeを適用する(MarkAll / MarkRange / MarkSearchHitsの共通部)。
        // マークの状態が変わったときだけ、描き直す(1回)
        MarkResult MarkRows(MarkMode mode, MarkKind kind, const std::function<bool(int)>& in_scope);

        // 検索語に一致する行(ヒット)を、今のlist_から作り直してsearch_に渡す。list_を作り直したとき(ApplyFilter)と、
        // 検索語・検索の状態が変わったときに呼ぶ。
        void RebuildSearchHits();
        // 位置(パス)を、今のlist_の添字にする。パスが見つからなければ、位置の添字(範囲に収める)。一覧が空なら-1
        int ResolveRow(const ListPosition& position) const;
        ListPosition PositionAt(int row) const;

        // 絞り込む前の全体の並び(sorted_)から、今の語に一致する行を選んで、list_とrow_of_sorted_と、絞り込みの
        // 一致箇所を作り直し、検索のヒットも作り直す。list_を書くのは、これだけ(Fetch()と、語が変わったとき(RefilterAround)に呼ぶ)。語が空なら全行。
        void ApplyFilter();
        // pivotのファイル(全体の並びでの位置)を基準に、絞り込み直して、カーソルをそのファイル(隠れたら近くの見える行)へ寄せる
        void RefilterAround(const ListPosition& pivot);
        // 位置を、sorted_(絞り込む前の全体の並び)の添字にする。パスが見つからなければ、位置の添字(範囲に収める)。全体が空なら-1
        int ResolveSortedIndex(const ListPosition& position) const;
        // 位置のファイルの近くの、見えている行(list_の添字)。そのファイルが見えていればその行。隠れていれば、全体の並びで
        // それ以降で最初に見える行 → 無ければ手前で最後に見える行。何も見えなければ0。ApplyFilter()の後に呼ぶ
        int ShownRowNear(const ListPosition& position) const;
        // list_のrow行目の、全体の並びでの位置
        ListPosition FullPositionAt(int row) const;
        // カーソルのファイルの、全体の並びでの位置。一覧が空(0件に絞り込んだ等)なら、絞り込みの基準を引き継ぐ
        ListPosition CursorOrAnchor() const;
        // 検索や、マーク済みの行へ動く(StepMark)ために、rowの行へカーソルを動かして、描き直す。今見えている範囲の外へ
        // 飛ぶときは、行を画面の中央に出す(Redraw()のスクロールは、行が見える最小限だけなので、遠くへ飛ぶと端に張り付く)。
        void JumpCursorTo(int row);

        // ディレクトリ監視。表示するパスが変わったときだけ張り直す。監視はパス基準で、ディレクトリが
        // 差し替えられても届き続けるので、同じパスの再スキャンでは張り直さない(張り直す間に起きた
        // 変更を取りこぼさないため)。
        void WatchDirectory(const std::filesystem::path& dir);
        void OnDirectoryChanged();

        // Reload()の間だけ持つ、カーソル復元用の記録。再スキャンで旧エントリが破棄される前に
        // パスとして控えておく(破棄後にlist_経由で読むと寿命切れの参照になる)。
        struct CursorMemo {
            std::vector<std::filesystem::path> order; // 再スキャン前の画面上の並び
            int index = 0;                            // その中でのカーソル位置
            std::optional<std::filesystem::path> target; // 指定があれば、再スキャン後にカーソルを合わせる先
        };
        // Fetch()で並べ直した後のlist_から、memoに基づくカーソル位置を求める
        int RestoreCursor(const CursorMemo& memo) const;
        std::optional<CursorMemo> reload_memo_;

        int cursorIndex_ = 0;
        bool focus_ = false;
        SortKey sort_key_ = SortKey::Name;
        bool sort_reverse_ = false;
        models::FileListModel& model_;
        // 画面に出す行(絞り込んだ後。画面の並び)。画面の行番号(cursorIndex_・検索のヒット・描画)は、すべてこの添字
        std::vector<FileEntryView*> list_;
        std::vector<FileEntryView> entries_;
        // 絞り込む前の全エントリ(ソート済み)。entries_の要素を指す。Fetch()で作り、語が変わるたびに、ここから選び直す
        std::vector<FileEntryView*> sorted_;
        // sorted_[i]がlist_の何行目か(絞り込みで隠れていれば-1)。sorted_と同じ長さ
        std::vector<int> row_of_sorted_;
        // 絞り込みと検索の状態。一致箇所とヒットは、list_(の添字)を使うので、list_を作り直すApplyFilter()のたびに作り直す。
        // 購読(subscriptions_)の通知はFetch()を呼ぶので、購読より先に宣言する(破棄は逆順)
        FilterState filter_;
        SearchState search_;
        std::vector<misc::SubscriptionGuard> subscriptions_;
        std::function<bool()> drag_guard_;
        std::vector<DragEntry> drag_entries_;
        std::filesystem::path drag_source_dir_; // ドラッグ開始時に表示していたディレクトリ

        std::filesystem::path watched_dir_; // watch_が見ているパス
        bool stale_ = false; // 監視で変化を検知し、まだ一覧に反映していない
        std::chrono::steady_clock::time_point reload_due_;          // 自動リロードしてよい時刻
        std::chrono::steady_clock::time_point next_reload_allowed_; // 直近の自動リロードの重さに応じた、次回の下限

        struct Impl;
        std::unique_ptr<Impl> impl_;
        // 破棄の順序: 他のメンバーより先に監視を止める(コールバックがthisを参照するため)ので最後に置く
        std::unique_ptr<pl_dir_watch> watch_;
    };
}

#endif // VIEWS_FILE_LIST_VIEW_H__

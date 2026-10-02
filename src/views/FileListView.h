#ifndef VIEWS_FILE_LIST_VIEW_H__
#define VIEWS_FILE_LIST_VIEW_H__

#include <chrono>
#include <expected>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "../models/Model.h"
#include "../misc.h"
#include "../platform.h"
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

        bool GetFocus() const { return focus_; }
        void SetFocus(bool focus);
        int GetCursor() const { return cursorIndex_; }
        void SetCursor(int index);
        void MoveCursor(int offset);
        inline FileEntryView& GetEntry(int index) const
        {
            return *list_[(size_t)index];
        }
        // カーソル下のエントリ。一覧が空だと使えない(範囲外を読む。空になり得る場所ではCurrentOrNull()を使う)
        inline FileEntryView& GetCurrent() const
        {
            return GetEntry(cursorIndex_);
        }
        // カーソル下のエントリ。一覧が空(カーソルが一覧の外)ならnullptr
        inline FileEntryView* CurrentOrNull() const
        {
            if (cursorIndex_ < 0 || (size_t)cursorIndex_ >= list_.size()) return nullptr;
            return list_[(size_t)cursorIndex_];
        }
        // カーソル下のファイルのパス。一覧が空ならnullopt(GetCurrent()は一覧が空だと使えない)
        std::optional<std::filesystem::path> CurrentPath() const;

        // カーソル移動やフォーカス変更を伴わない外部要因(マーク変更等)の後に呼ぶ再描画要求
        void Redraw();

        // マークをすべて解除して、再描画を要求する。モデルのClearMarks()はフラグを書き換えるだけで
        // ビューには通知されないので、画面に反映したいときはこちらを使う。
        void ClearMarks();

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
        // ドラッグ開始時に呼ぶ。このペインのマーク済みエントリを画面表示順で返す。
        // どの行を押してドラッグしたかは問わない。マークが無い、またはDragGuardが
        // 拒否した場合は空を返す(=ドラッグしない)。
        const std::vector<DragEntry>& BeginDrag();
        // ドラッグ終了時に呼ぶ。acceptedは宛先がドロップを受理したか(キャンセル/拒否ならfalse)。
        // 受理された場合、ファイルが移動されていればReload()で一覧を最新にし(カーソルは維持、
        // 移されなかったファイルのマークは残る)、そうでなければマークだけ解除する。
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
        SearchMode GetSearchMode() const { return search_.Mode(); }
        // 検索バーに出す内容(語、ヒット数、カーソルのある行の順番)
        SearchStatus GetSearchStatus() const;

        // 一覧の下端に空ける高さ(pt)。一覧(スクロール部分)がこの分だけ縮む。空いた帯には、親(BrowserView)が
        // このペインの検索バーを重ねる(反対側のペインは縮まない)。0で空けない。縮んでカーソルが見えなくなるときは、
        // 見える位置までスクロールする。
        void SetBottomInset(double height);

    private:
        void Fetch();

        // 検索語に一致する行(ヒット)を、今のlist_から作り直してsearch_に渡す。list_を作り直したとき(Fetch)と、
        // 検索語・検索の状態が変わったときに呼ぶ。
        void RebuildSearchHits();
        // 位置(パス)を、今のlist_の添字にする。パスが見つからなければ、位置の添字(範囲に収める)。一覧が空なら-1
        int ResolveRow(const SearchPosition& position) const;
        SearchPosition PositionAt(int row) const;
        // 検索でrowの行へカーソルを動かして、描き直す。今見えている範囲の外へ飛ぶときは、行を画面の中央に出す
        // (Redraw()のスクロールは、行が見える最小限だけなので、遠くへ飛ぶと端に張り付く)。
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
        std::vector<FileEntryView*> list_;
        std::vector<FileEntryView> entries_;
        // 検索の状態。ヒットは表示順(list_)の添字なので、list_を作り直すFetch()のたびに作り直す。
        // 購読(subscriptions_)の通知はFetch()を呼ぶので、購読より先に宣言する(破棄は逆順)
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

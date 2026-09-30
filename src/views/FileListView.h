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
        // それぞれ対応するNSViewのdrawRect:から呼ばれる。dirtyRectは無視して
        // 常に全体を再描画する(行数が少ないため十分)。呼び出し元のビュー自身の
        // 座標系で描画するため、ヘッダーとリスト本体でメソッドを分けている。
        void DrawHeader();
        void Draw();

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
        inline FileEntryView& GetCurrent() const
        {
            return GetEntry(cursorIndex_);
        }

        // カーソル移動やフォーカス変更を伴わない外部要因(マーク変更等)の後に呼ぶ再描画要求
        void Redraw();

        // このペインのディレクトリを再スキャンする(FileListModel::Reload()参照)。
        // マークもカーソルも、パスで同じファイルを引き継ぐ。カーソルのファイルが消えていた
        // 場合は、再スキャン前の画面上の並びで次に残っているファイル(無ければその前)へ寄せる。
        // cursor_toを渡すと、再スキャン後にそのパスのファイルへカーソルを合わせる(リネームの
        // 直後など、旧パスが消えて新しいパスに移るとき用。一覧に無ければ上記の通常の寄せ方)。
        // 失敗時(ディレクトリが読めない等)は何も変えずにエラーメッセージを返す。
        std::expected<void, std::string> Reload(std::optional<std::filesystem::path> cursor_to = std::nullopt);

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

    private:
        void Fetch();

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

#ifndef VIEWS_FILE_LIST_VIEW_H__
#define VIEWS_FILE_LIST_VIEW_H__

#include <filesystem>
#include <functional>
#include <memory>
#include <vector>
#include "../models/Model.h"
#include "../misc.h"

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
        // 受理された場合、ファイルが移動されていれば一覧を再スキャンし、そうでなければマークだけ解除する。
        void EndDrag(bool accepted);

    private:
        void Fetch();

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

        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}

#endif // VIEWS_FILE_LIST_VIEW_H__

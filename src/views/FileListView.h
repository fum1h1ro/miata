#ifndef VIEWS_FILE_LIST_VIEW_H__
#define VIEWS_FILE_LIST_VIEW_H__

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
        // drawRect: から呼ばれる。dirtyRectは無視して常に全体を再描画する(行数が少ないため十分)。
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

        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}

#endif // VIEWS_FILE_LIST_VIEW_H__

#ifndef VIEWS_FILE_LIST_VIEW_H__
#define VIEWS_FILE_LIST_VIEW_H__

#include <vector>
#include <string>
#include <imgui.h>
#include "../widgets/Widget.h"
#include "../models/Model.h"
#include "../Config.h"

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

    class FileListView : public widgets::Pane {
    public:
        FileListView(const char* id, int w, int h, models::FileListModel& list);
        void OnGuiImpl(bool window_resized) override;

        void Clear()
        {
            cursorIndex_ = 0;
        }
        bool GetFocus() const
        {
            return focus_;
        }
        void SetFocus(bool focus)
        {
            focus_ = focus;
        }
        int GetCursor() const
        {
            return cursorIndex_;
        }
        void SetCursor(int index)
        {
            cursorIndex_ = index;
        }
        void MoveCursor(int offset)
        {
            cursorIndex_ += offset;
        }
        inline FileEntryView& GetEntry(int index) const
        {
            return *list_[(size_t)index];
        }
        inline FileEntryView& GetCurrent() const
        {
            return GetEntry(cursorIndex_);
        }
    private:
        bool IsFullyVisible() const;
        void Fetch();
        void OnGuiHeader();
        void OnGuiList();
        //
        int cursorIndex_;
        bool focus_;
        models::FileListModel& model_;
        std::vector<FileEntryView*> list_;
        std::vector<FileEntryView> entries_;
        std::vector<misc::SubscriptionGuard> subscriptions_;
    };
}

#endif // VIEWS_FILE_LIST_VIEW_H__

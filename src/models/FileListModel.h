#ifndef MODELS_FILE_LIST_MODEL_H__
#define MODELS_FILE_LIST_MODEL_H__

#include <filesystem>
#include "../misc.h"
#include "FileEntryModel.h"

namespace miata::models {
    class FileListModel {
    public:
        FileListModel();
        ~FileListModel();
        void JumpTo(const std::filesystem::path& path);
        void NavigateToParent();
        void Invoke(int idx);
        void Mark(int idx);
        void Unmark(int idx);
        bool IsMarked(int idx);
        void ToggleMark(int idx);
        void ClearMarks();
        std::filesystem::path Path() const
        {
            return path_.Value();
        }
        const int Size() const
        {
            return (int)entries_.size();
        }
        FileEntryModel& GetEntry(int idx)
        {
            return *entries_[(size_t)idx];
        }
        rxcpp::observable<std::filesystem::path> ObservePath()
        {
            return path_.Observe();
        }
    private:
        misc::ReactiveProperty<std::filesystem::path> path_;
        std::vector<std::unique_ptr<FileEntryModel>> entries_;
    };
}





#endif // MODELS_FILE_LIST_MODEL_H__

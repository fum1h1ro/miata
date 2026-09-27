#include "../platform.h"
#include "FileListModel.h"
#include <memory>

namespace miata::models {
    FileListModel::FileListModel()
    {
        entries_.reserve(1024);
        auto home = pl_get_home_dir();
        JumpTo(home);
    }

    FileListModel::~FileListModel()
    {
    }

    void FileListModel::JumpTo(const std::filesystem::path& path)
    {
        entries_.clear();
        for (auto& d : std::filesystem::directory_iterator(path)) {
            entries_.emplace_back(std::unique_ptr<FileEntryModel>(new FileEntryModel(d)));
        }
        path_.Value(path);
    }

    void FileListModel::NavigateToParent()
    {
        if (path_.Value().has_parent_path()) {
            JumpTo(path_.Value().parent_path());
        }
    }

    void FileListModel::Invoke(int idx)
    {
        if (idx < 0 || idx >= entries_.size()) return;
        auto& entry = entries_[(size_t)idx];
        printf("invoke: %s\n", entry->Name().c_str());
        if (entry->IsDirectory()) {
            printf("go to: %s\n", (path_.Value() / entry->Name()).c_str());
            auto new_path = path_.Value() / entry->Name();
            JumpTo(new_path);
        }
    }

    void FileListModel::Mark(int idx)
    {
        if (idx < 0 || idx >= entries_.size()) return;
        auto& entry = entries_[(size_t)idx];
        entry->Mark(true);
    }

    void FileListModel::Unmark(int idx)
    {
        if (idx < 0 || idx >= entries_.size()) return;
        auto& entry = entries_[(size_t)idx];
        entry->Mark(false);
    }

    bool FileListModel::IsMarked(int idx)
    {
        if (idx < 0 || idx >= entries_.size()) return false;
        auto& entry = entries_[(size_t)idx];
        return entry->IsMarked();
    }

    void FileListModel::ToggleMark(int idx)
    {
        if (IsMarked(idx)) {
            Unmark(idx);
        }
        else {
            Mark(idx);
        }
    }

    void FileListModel::ClearMarks()
    {
        for (auto& entry : entries_) {
            entry->Mark(false);
        }
    }
}

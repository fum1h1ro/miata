#include "../platform.h"
#include "FileListModel.h"
#include <memory>
#include <unordered_set>

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

    std::expected<void, std::string> FileListModel::Reload()
    {
        auto path = path_.Value();

        // 先に新しい一覧を作り、失敗した場合は現在の状態に一切触れない。
        // 再読み込みは外部でディレクトリが変わった後に使うため、JumpToと違って
        // 消えた/権限の無いディレクトリでも例外を投げずにエラーとして返す。
        std::vector<std::unique_ptr<FileEntryModel>> scanned;
        std::error_code ec;
        std::filesystem::directory_iterator it(path, ec);
        for (; !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
            scanned.emplace_back(std::unique_ptr<FileEntryModel>(new FileEntryModel(*it)));
        }
        if (ec) return std::unexpected(ec.message());

        std::unordered_set<std::filesystem::path> marked;
        for (auto& entry : entries_) {
            if (entry->IsMarked()) marked.insert(entry->Path());
        }
        for (auto& entry : scanned) {
            if (marked.contains(entry->Path())) entry->Mark(true);
        }

        entries_ = std::move(scanned);
        path_.Value(path);
        return {};
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

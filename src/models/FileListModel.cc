#include "../platform.h"
#include "FileListModel.h"
#include <memory>
#include <unordered_set>

namespace miata::models {
    namespace {
        // ディレクトリの更新日時(直下のエントリの追加・削除・改名で変わる)。取得できなければ空。
        std::optional<std::filesystem::file_time_type> DirectoryMtime(const std::filesystem::path& path)
        {
            std::error_code ec;
            auto mtime = std::filesystem::last_write_time(path, ec);
            if (ec) return std::nullopt;
            return mtime;
        }
    }

    // FileEntryModelのコンストラクタはFileListModelだけが呼べるので、メンバー(private)にしている
    std::expected<std::vector<std::unique_ptr<FileEntryModel>>, std::error_code> FileListModel::Scan(const std::filesystem::path& dir)
    {
        std::vector<std::unique_ptr<FileEntryModel>> scanned;
        std::error_code ec;
        std::filesystem::directory_iterator it(dir, ec);
        for (; !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
            scanned.emplace_back(std::unique_ptr<FileEntryModel>(new FileEntryModel(*it)));
        }
        if (ec) return std::unexpected(ec);
        return scanned;
    }

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
        scanned_mtime_ = DirectoryMtime(path); // 走査より前に取る
        entries_.clear();
        for (auto& d : std::filesystem::directory_iterator(path)) {
            entries_.emplace_back(std::unique_ptr<FileEntryModel>(new FileEntryModel(d)));
        }
        path_.Value(path);
    }

    std::expected<void, FileError> FileListModel::TryJumpTo(const std::filesystem::path& path)
    {
        // 先に新しい一覧を作り、失敗した場合は現在の状態(一覧もパスも)に一切触れない
        auto mtime = DirectoryMtime(path); // 走査より前に取る
        auto scanned = Scan(path);
        if (!scanned) return std::unexpected(FileError::From(scanned.error()));

        scanned_mtime_ = mtime;
        entries_ = std::move(*scanned);
        history_.Record(path); // 通知より前に記録する(購読側が履歴を読んでも、この移動が入っている)
        path_.Value(path);
        return {};
    }

    std::expected<void, FileError> FileListModel::Reload()
    {
        auto path = path_.Value();

        // 先に新しい一覧を作り、失敗した場合は現在の状態に一切触れない。
        // 再読み込みは外部でディレクトリが変わった後に使うため、JumpToと違って
        // 消えた/権限の無いディレクトリでも例外を投げずにエラーとして返す。
        auto mtime = DirectoryMtime(path); // 走査より前に取る
        auto scanned = Scan(path);
        if (!scanned) return std::unexpected(FileError::From(scanned.error()));

        std::unordered_set<std::filesystem::path> marked;
        for (auto& entry : entries_) {
            if (entry->IsMarked()) marked.insert(entry->Path());
        }
        for (auto& entry : *scanned) {
            if (marked.contains(entry->Path())) entry->Mark(true);
        }

        entries_ = std::move(*scanned);
        scanned_mtime_ = mtime;
        path_.Value(path);
        return {};
    }

    std::expected<void, FileError> FileListModel::NavigateToParent()
    {
        if (!path_.Value().has_parent_path()) return {};
        return TryJumpTo(path_.Value().parent_path());
    }

    std::expected<void, FileError> FileListModel::Invoke(int idx)
    {
        if (idx < 0 || idx >= entries_.size()) return {};
        auto& entry = entries_[(size_t)idx];
        printf("invoke: %s\n", entry->Name().c_str());
        if (!entry->IsDirectory()) return {};
        printf("go to: %s\n", (path_.Value() / entry->Name()).c_str());
        return TryJumpTo(path_.Value() / entry->Name());
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

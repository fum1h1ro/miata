#ifndef MODELS_FILE_LIST_MODEL_H__
#define MODELS_FILE_LIST_MODEL_H__

#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "../misc.h"
#include "FileEntryModel.h"

namespace miata::models {
    class FileListModel {
    public:
        FileListModel();
        ~FileListModel();
        void JumpTo(const std::filesystem::path& path);
        // 現在のディレクトリを再スキャンする。JumpTo(Path())と違い、マークは同じパスの
        // エントリに引き継ぐ(消えたファイルのマークは落ち、新しいファイルは未マーク)。
        // ディレクトリが消えた/読めない場合は、何も変えずにエラーメッセージを返す。
        // 成功時はJumpToと同様にObservePath()へ通知する(購読側は旧エントリへの参照を
        // 捨てて作り直す必要がある。旧エントリは通知の前に破棄される)。
        std::expected<void, std::string> Reload();
        // 直近に走査(JumpTo/Reload)したときのディレクトリの更新日時。走査の直前に取るので、これより
        // 後にディレクトリの中身が変わっていれば、現在の更新日時と食い違う(走査から変更の監視を
        // 始めるまでの間の変更を見逃さないための確認用)。取得できなかった場合は空。
        std::optional<std::filesystem::file_time_type> ScannedMtime() const
        {
            return scanned_mtime_;
        }
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
        std::optional<std::filesystem::file_time_type> scanned_mtime_;
    };
}





#endif // MODELS_FILE_LIST_MODEL_H__

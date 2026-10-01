#ifndef MODELS_FILE_LIST_MODEL_H__
#define MODELS_FILE_LIST_MODEL_H__

#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "../FileError.h"
#include "../misc.h"
#include "FileEntryModel.h"

namespace miata::models {
    class FileListModel {
    public:
        FileListModel();
        ~FileListModel();
        // ディレクトリへ移動する。読めない(権限が無い・消えた等)と例外(std::filesystem::filesystem_error)を
        // 投げる。ユーザーの操作による移動では、落ちないよう、エラーを返すTryJumpToを使うこと。
        void JumpTo(const std::filesystem::path& path);
        // JumpToの、例外を投げない版。読めない場合は、一覧もパスも変えずにエラーを返す(権限が無い失敗は
        // FileError::permission_deniedで分かる)。成功時はJumpToと同じ(ObservePath()へ通知する)。
        std::expected<void, FileError> TryJumpTo(const std::filesystem::path& path);
        // 現在のディレクトリを再スキャンする。JumpTo(Path())と違い、マークは同じパスの
        // エントリに引き継ぐ(消えたファイルのマークは落ち、新しいファイルは未マーク)。
        // ディレクトリが消えた/読めない場合は、何も変えずにエラーを返す。
        // 成功時はJumpToと同様にObservePath()へ通知する(購読側は旧エントリへの参照を
        // 捨てて作り直す必要がある。旧エントリは通知の前に破棄される)。
        std::expected<void, FileError> Reload();
        // 直近に走査(JumpTo/Reload)したときのディレクトリの更新日時。走査の直前に取るので、これより
        // 後にディレクトリの中身が変わっていれば、現在の更新日時と食い違う(走査から変更の監視を
        // 始めるまでの間の変更を見逃さないための確認用)。取得できなかった場合は空。
        std::optional<std::filesystem::file_time_type> ScannedMtime() const
        {
            return scanned_mtime_;
        }
        // 親ディレクトリ/カーソル位置のディレクトリへ移動する(TryJumpTo。失敗したら何も変えずにエラーを返す)
        std::expected<void, FileError> NavigateToParent();
        std::expected<void, FileError> Invoke(int idx);
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
        // dirの直下を走査して、エントリのモデルを作る。例外は投げない(読めない・消えた・権限が無い等は
        // エラーコードで返す)。
        static std::expected<std::vector<std::unique_ptr<FileEntryModel>>, std::error_code> Scan(const std::filesystem::path& dir);

        misc::ReactiveProperty<std::filesystem::path> path_;
        std::vector<std::unique_ptr<FileEntryModel>> entries_;
        std::optional<std::filesystem::file_time_type> scanned_mtime_;
    };
}





#endif // MODELS_FILE_LIST_MODEL_H__

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
    class PathHistory;

    class FileListModel {
    public:
        // historyは、移動に成功したフォルダの記録先(フォルダの履歴。左右のペインで1つを共有するので、BrowserModelが
        // 持つものを渡す)。nullptrなら記録しない。
        explicit FileListModel(PathHistory* history = nullptr);
        ~FileListModel();
        // ディレクトリへ移動する。読めない(権限が無い・消えた等)と例外(std::filesystem::filesystem_error)を
        // 投げる。ユーザーの操作による移動では、落ちないよう、エラーを返すTryJumpToを使うこと。
        // 履歴には記録しない(起動時のホームが、履歴に入らないようにするため)。
        void JumpTo(const std::filesystem::path& path);
        // JumpToの、例外を投げない版。読めない場合は、一覧もパスも変えずにエラーを返す(権限が無い失敗は
        // FileError::permission_deniedで分かる)。成功時はJumpToと同じ(ObservePath()へ通知する)が、それに加えて、
        // 移動先を履歴(コンストラクタで渡されたもの)に記録する。履歴に記録するのはここだけ: JumpTo(起動時のホーム)と
        // Reload(再スキャン)は記録しない。ユーザー操作の移動(Enter・親へ戻る・履歴からのジャンプ)は、すべてここを通る。
        std::expected<void, FileError> TryJumpTo(const std::filesystem::path& path);
        // 起動時に、前回のペインの場所を戻すための移動。TryJumpToと同じ(読めなければ、一覧もパスも変えずにエラーを返す)
        // だが、履歴には記録しない: 復元は、ユーザーの移動ではないので、履歴の並びを動かさない。
        std::expected<void, FileError> TryRestoreTo(const std::filesystem::path& path);
        // 現在のディレクトリを再スキャンする。JumpTo(Path())と違い、マークは同じパスの
        // エントリに引き継ぐ(消えたファイルのマークは落ち、新しいファイルは未マーク)。
        // ディレクトリが消えた/読めない場合は、何も変えずにエラーを返す。
        // 成功時はJumpToと同様にObservePath()へ通知する(購読側は旧エントリへの参照を
        // 捨てて作り直す必要がある。旧エントリは通知の前に破棄される)。履歴には記録しない。
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
        // TryJumpTo / TryRestoreToの本体。record_historyがtrueのときだけ、移動先を履歴に記録する
        std::expected<void, FileError> TryJumpToImpl(const std::filesystem::path& path, bool record_history);
        // dirの直下を走査して、エントリのモデルを作る。例外は投げない(読めない・消えた・権限が無い等は
        // エラーコードで返す)。
        static std::expected<std::vector<std::unique_ptr<FileEntryModel>>, std::error_code> Scan(const std::filesystem::path& dir);

        misc::ReactiveProperty<std::filesystem::path> path_;
        PathHistory* history_; // 移動に成功したフォルダの記録先(持ち主はBrowserModel。nullptrなら記録しない)
        std::vector<std::unique_ptr<FileEntryModel>> entries_;
        std::optional<std::filesystem::file_time_type> scanned_mtime_;
    };
}





#endif // MODELS_FILE_LIST_MODEL_H__

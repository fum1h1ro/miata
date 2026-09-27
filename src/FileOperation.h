#ifndef FILE_OPERATION_H__
#define FILE_OPERATION_H__

#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>
#include "models/FileListModel.h"

namespace miata {
    enum class FileOpType {
        Copy,
        Move,
    };

    // FileOperationManager からコールバックで通知される完了イベント。
    // src_dir / dest_dir は操作開始時点でのペインの表示パスのスナップショット。
    // 完了時、該当ペインが今もこのパスを表示している場合のみ再スキャンするために使う
    // （バックグラウンド実行中に別ディレクトリへ移動された場合に誤って再スキャンしないため）。
    struct FileOperationCompleted {
        FileOpType type;
        bool success;
        int failed_count;
        std::string error_message;
        models::FileListModel* src_model;
        models::FileListModel* dest_model;
        std::filesystem::path src_dir;
        std::filesystem::path dest_dir;
    };

    // ファイルのコピー・移動をバックグラウンドスレッドで実行する。
    // Job はファイルI/Oのみを行い、Lua/ImGui/View 等のメインスレッド専用オブジェクトには一切触れない。
    // 完了は Update() を呼んだスレッド（メインスレッド）上でコールバックを通じて通知される。
    class FileOperationManager {
    public:
        ~FileOperationManager();

        void SetCallback(std::function<void(FileOperationCompleted&)> callback);

        void Start(
            FileOpType type,
            std::vector<std::filesystem::path> sources,
            std::filesystem::path src_dir,
            std::filesystem::path dest_dir,
            bool overwrite,
            models::FileListModel* src_model,
            models::FileListModel* dest_model
        );

        // Application::FrameImpl から毎フレーム呼ぶ。完了したジョブを検出して通知する。
        void Update();

    private:
        struct Job {
            std::thread thread_;
            std::mutex mutex_;
            std::optional<FileOperationCompleted> result_;
        };

        static void Run(
            Job* job,
            FileOpType type,
            std::vector<std::filesystem::path> sources,
            std::filesystem::path src_dir,
            std::filesystem::path dest_dir,
            bool overwrite,
            models::FileListModel* src_model,
            models::FileListModel* dest_model
        );

        std::function<void(FileOperationCompleted&)> callback_;
        std::vector<std::unique_ptr<Job>> jobs_;
    };
}

#endif // FILE_OPERATION_H__

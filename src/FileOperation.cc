#include <system_error>
#include "FileOperation.h"

namespace miata {
    FileOperationManager::~FileOperationManager()
    {
        // アプリ終了時にまだ動いているスレッドが残っていると
        // std::thread のデストラクタが terminate() を呼ぶため、detach して安全に終了させる。
        for (auto& job : jobs_) {
            if (job->thread_.joinable()) {
                job->thread_.detach();
            }
        }
    }

    void FileOperationManager::SetCallback(std::function<void(FileOperationCompleted&)> callback)
    {
        callback_ = std::move(callback);
    }

    void FileOperationManager::Start(
        FileOpType type,
        std::vector<std::filesystem::path> sources,
        std::filesystem::path src_dir,
        std::filesystem::path dest_dir,
        bool overwrite,
        models::FileListModel* src_model,
        models::FileListModel* dest_model
    )
    {
        auto job = std::make_unique<Job>();
        auto job_ptr = job.get();
        job->thread_ = std::thread(
            Run, job_ptr, type, std::move(sources), std::move(src_dir), std::move(dest_dir), overwrite, src_model, dest_model
        );
        jobs_.push_back(std::move(job));
    }

    void FileOperationManager::Update()
    {
        for (auto it = jobs_.begin(); it != jobs_.end(); ) {
            auto& job = *it;
            std::optional<FileOperationCompleted> result;
            {
                std::lock_guard<std::mutex> lock(job->mutex_);
                result = job->result_;
            }
            if (result) {
                job->thread_.join();
                if (callback_) callback_(*result);
                it = jobs_.erase(it);
            }
            else {
                ++it;
            }
        }
    }

    void FileOperationManager::Run(
        Job* job,
        FileOpType type,
        std::vector<std::filesystem::path> sources,
        std::filesystem::path src_dir,
        std::filesystem::path dest_dir,
        bool overwrite,
        models::FileListModel* src_model,
        models::FileListModel* dest_model
    )
    {
        auto options = overwrite
            ? (std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing)
            : (std::filesystem::copy_options::recursive | std::filesystem::copy_options::skip_existing);

        FileErrorSummary failures;

        for (auto& src : sources) {
            auto dst = dest_dir / src.filename();
            std::error_code ec;

            if (type == FileOpType::Copy) {
                std::filesystem::copy(src, dst, options, ec);
            }
            else {
                std::error_code exists_ec;
                if (std::filesystem::exists(dst, exists_ec)) {
                    if (!overwrite) continue; // ユーザーがスキップを選択済み。rename()の暗黙の上書きに頼らない
                    std::filesystem::remove_all(dst, ec);
                    if (ec) {
                        failures.Add(FileError::From(ec));
                        continue;
                    }
                }
                std::filesystem::rename(src, dst, ec);
                if (ec == std::errc::cross_device_link) {
                    // 異なるボリューム間の move は rename できないため、コピー+削除にフォールバックする。
                    ec.clear();
                    std::filesystem::copy(src, dst, options, ec);
                    if (!ec) {
                        std::filesystem::remove_all(src, ec);
                    }
                }
            }

            if (ec) {
                failures.Add(FileError::From(ec));
            }
        }

        FileOperationCompleted completed{
            .type = type,
            .success = failures.count == 0,
            .failed_count = failures.count,
            .error_message = failures.shown.message,
            .permission_denied = failures.shown.permission_denied,
            .src_model = src_model,
            .dest_model = dest_model,
            .src_dir = src_dir,
            .dest_dir = dest_dir,
            .sources = std::move(sources),
        };

        std::lock_guard<std::mutex> lock(job->mutex_);
        job->result_ = std::move(completed);
    }
}

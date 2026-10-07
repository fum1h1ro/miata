#include <sys/stat.h>
#include <algorithm>
#include <format>
#include <system_error>
#include "FileOperation.h"

namespace miata {
    namespace {
        struct EntryId {
            std::pair<std::uint64_t, std::uint64_t> id; // (st_dev, st_ino)
            bool is_dir;
        };

        // pそのもの(シンボリックリンクはたどらない)の実体。調べられなければ nullopt
        std::optional<EntryId> IdOf(const std::filesystem::path& path)
        {
            struct stat st;
            if (::lstat(path.c_str(), &st) != 0) return std::nullopt;
            return EntryId{{(std::uint64_t)st.st_dev, (std::uint64_t)st.st_ino}, S_ISDIR(st.st_mode) != 0};
        }

        template <class T>
        bool Contains(const std::vector<T>& values, const T& value)
        {
            return std::find(values.begin(), values.end(), value) != values.end();
        }
    }

    FileOperationGuard::FileOperationGuard(std::filesystem::path dest_dir)
        : dest_dir_(std::move(dest_dir)), dest_chain_(ChainOf(dest_dir_))
    {
    }

    FileOperationGuard::Chain FileOperationGuard::ChainOf(const std::filesystem::path& path)
    {
        Chain chain;
        std::error_code ec;
        // シンボリックリンクを先に解決して、実際の場所の祖先をたどる(存在しない末尾は、解決せずに残る)
        auto real = std::filesystem::weakly_canonical(path, ec);
        if (ec) return chain;
        for (auto current = real;; current = current.parent_path()) {
            if (auto entry = IdOf(current)) chain.ids.push_back(entry->id); // 存在しない部分は飛ばす
            if (current == current.parent_path()) break;                     // ルート
        }
        chain.ok = !chain.ids.empty();
        return chain;
    }

    std::optional<std::string> FileOperationGuard::Check(FileOpType type, const std::filesystem::path& src, bool& unknown)
    {
        unknown = false;
        const char* verb = type == FileOpType::Copy ? "コピー" : "移動";

        auto src_entry = IdOf(src);
        if (!dest_chain_.ok || !src_entry) {
            // 先の場所か元を調べられない(元が外部で消えた、権限が無いなど)。操作は、項目の失敗として知らせる
            unknown = true;
            return std::nullopt;
        }

        // (2) 先のフォルダが、元のフォルダの中(またはそのもの)
        if (src_entry->is_dir && Contains(dest_chain_.ids, src_entry->id)) {
            return std::format("フォルダを、その中のフォルダへ{}することはできません", verb);
        }

        // 先の同名の項目が無ければ、消える・上書きされるものは無い
        auto dst_entry = IdOf(dest_dir_ / src.filename());
        if (!dst_entry) return std::nullopt;

        // 元の祖先(親フォルダから、ルートまで)
        auto parent = src.parent_path();
        if (!parent_chain_.ok || parent != cached_parent_) {
            cached_parent_ = parent;
            parent_chain_ = ChainOf(parent);
        }
        if (!parent_chain_.ok) {
            unknown = true;
            return std::nullopt;
        }

        // (1) 先が、元と同じ実体
        if (dst_entry->id == src_entry->id) {
            if (dest_chain_.ids.front() == parent_chain_.ids.front()) {
                return std::format("{}先が、{}元と同じフォルダです", verb, verb);
            }
            return std::format("{}先に、{}元と同じ実体のファイルがあります", verb, verb);
        }

        // (3) 先の同名のフォルダが、元の祖先。Moveの「上書き」だけが壊す(Copyは、フォルダへ重ねるだけ)
        if (type == FileOpType::Move && dst_entry->is_dir && Contains(parent_chain_.ids, dst_entry->id)) {
            return std::format("移動先の同名のフォルダの中に移動元があるため、上書きできません");
        }
        return std::nullopt;
    }

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
        FileOperationGuard guard(dest_dir);

        for (auto& src : sources) {
            auto dst = dest_dir / src.filename();
            std::error_code ec;

            // 始める前の確認(Application::StartFileOperation)と同じ確認を、コピー・消す直前にもやる(多重の防御)。
            // 確認してから、ここに順番が来るまでに、状況が変わっていることがある
            bool unknown = false;
            if (auto reason = guard.Check(type, src, unknown)) {
                failures.Add(FileError{.message = *reason});
                continue;
            }

            if (type == FileOpType::Copy) {
                std::filesystem::copy(src, dst, options, ec);
            }
            else {
                std::error_code exists_ec;
                if (std::filesystem::exists(dst, exists_ec)) {
                    if (!overwrite) continue; // ユーザーがスキップを選択済み。rename()の暗黙の上書きに頼らない
                    if (unknown) {
                        // 元か先を調べられなかったので、先が元と同じものではないと言い切れない。消す前に諦める
                        failures.Add(FileError{.message = "移動元か移動先を調べられないので、上書きしません"});
                        continue;
                    }
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

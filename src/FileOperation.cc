#include <sys/stat.h>
#include <algorithm>
#include <cerrno>
#include <format>
#include <system_error>
#include <unordered_set>
#include "FileOperation.h"
#include "platform.h"

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

        std::error_code ErrorOf(std::errc code)
        {
            return std::make_error_code(code);
        }

        // pathそのもの(シンボリックリンクはたどらない)の stat。無い(ENOENT / ENOTDIR)ときは nullopt で、ec は空のまま。
        // ほかの失敗(権限が無いなど)は、ec に入れる
        std::optional<struct stat> LstatOf(const std::filesystem::path& path, std::error_code& ec)
        {
            ec.clear();
            struct stat st;
            if (::lstat(path.c_str(), &st) == 0) return st;
            if (errno != ENOENT && errno != ENOTDIR) ec = std::error_code(errno, std::generic_category());
            return std::nullopt;
        }

        // コピーの進み具合の出口(ワーカー=Run が、Job へ書く関数を渡す)
        struct CopySink {
            std::function<void(const std::filesystem::path& file)> file_started; // ファイル(リンク)を始める。名前を、進捗に出す
            std::function<void(std::int64_t bytes)> bytes_done;                 // 終えた(コピーした・スキップした)バイト数が増えた
        };

        // src(リンクそのもの)と dest_dir が、同じボリュームか。調べられなければ false(コピーとして数える)
        bool SameVolume(const std::filesystem::path& src, const std::filesystem::path& dest_dir)
        {
            struct stat a, b;
            return ::lstat(src.c_str(), &a) == 0 && ::stat(dest_dir.c_str(), &b) == 0 && a.st_dev == b.st_dev;
        }

        // 先(dst)に同名のファイル・リンクがあるときの扱い。フォルダは、どれでも、重ねる
        enum class OnConflict {
            Skip,      // 置かずに、終えたものとして数える(エラーではない)
            Overwrite, // 置き換える
            Fail,      // 失敗(EEXIST)にする。作ったばかりのフォルダの中と、別のボリュームへの移動のコピー(CopyEntry の説明)
        };

        std::error_code CopyEntry(const std::filesystem::path& src, const std::filesystem::path& dst, OnConflict on_conflict, const CopySink& sink);

        // フォルダ src を dst へコピーする。dst が無ければ作り、あればそこへ重ねる(同名のファイルは、on_conflict に従う)。
        // 最初のエラーで止まる(それまでに置いたものは残る)。
        std::error_code CopyDirectory(
            const std::filesystem::path& src,
            const std::filesystem::path& dst,
            OnConflict on_conflict,
            bool dst_exists,
            const CopySink& sink
        )
        {
            // 作るときの権限は、自分だけが読み書きできる 0700。src が読み取り専用(0555 など)でも、中身を置けるように、
            // 本当の権限は、置き終えてから付ける
            if (!dst_exists && ::mkdir(dst.c_str(), 0700) != 0) return std::error_code(errno, std::generic_category());

            std::error_code result;
            std::filesystem::directory_iterator it(src, result); // 開けない(権限が無い)ときも、ここで止まる
            while (!result && it != std::filesystem::directory_iterator()) {
                // 作ったばかりのフォルダ(dst_exists が false)の中の同名は、衝突(Fail。下の CopyEntry)
                result = CopyEntry(it->path(), dst / it->path().filename(), dst_exists ? on_conflict : OnConflict::Fail, sink);
                if (!result) it.increment(result);
            }

            if (!dst_exists) {
                // 作ったフォルダの属性(権限・更新日時・拡張属性)は、中身を置き終えた後に写す(先だと、更新日時が、
                // 中身を置いた時刻で上書きされる)。中身の失敗で止まったときも、写す(作りかけのフォルダの権限が 0700 のまま残らない)
                auto attributes = pl_copy_directory_attributes(src, dst);
                if (!result) result = attributes;
            }
            return result;
        }

        // src(ファイル・フォルダ・シンボリックリンク)を dst へコピーする。最初のエラーで止めて、それを返す。
        // リンクは、たどらずに、リンクそのものをコピーする(フォルダへのリンクを、中身ごと写して、自分の中へ増え続けることが無い)。
        // dst に同名があるとき: ファイルは on_conflict に従う(スキップ・置き換え・失敗)。フォルダは重ねる。
        // 種類が違うとき(ファイルとフォルダ)は、どちらも消さずに、エラーにする。
        // Fail は、作ったばかりのフォルダの中と、別のボリュームへの移動のコピー(移動は、成功したときだけ元を消す)で使う。そこに同名が
        // あるのは、この操作で先に置いたもの(大文字小文字・正規化の違いだけの名前が、同じになるボリュームへ。大文字小文字を区別する
        // ボリュームから、区別しないボリュームへのコピーで起きる)か、ほかが作ったもの。スキップも上書きもせずに失敗にしないと、
        // 置かれていないファイルの元が、移動で消える(上書きなら、先に置いたものが消える)
        std::error_code CopyEntry(const std::filesystem::path& src, const std::filesystem::path& dst, OnConflict on_conflict, const CopySink& sink)
        {
            std::error_code ec;
            auto src_stat = LstatOf(src, ec);
            if (ec) return ec;
            if (!src_stat) return ErrorOf(std::errc::no_such_file_or_directory); // 途中で消えた
            auto dst_stat = LstatOf(dst, ec);
            if (ec) return ec;
            if (dst_stat && on_conflict == OnConflict::Fail) return ErrorOf(std::errc::file_exists);

            if (S_ISDIR(src_stat->st_mode)) {
                if (dst_stat && !S_ISDIR(dst_stat->st_mode)) return ErrorOf(std::errc::not_a_directory); // 同名のファイル・リンクがある
                return CopyDirectory(src, dst, on_conflict, dst_stat.has_value(), sink);
            }
            if (!S_ISREG(src_stat->st_mode) && !S_ISLNK(src_stat->st_mode)) {
                // FIFO・ソケット・デバイス。copyfile は、デバイスを読み出して普通のファイルにしてしまう(/dev/null で、空の普通のファイルができた)ので、渡さない
                return ErrorOf(std::errc::not_supported);
            }
            if (dst_stat && S_ISDIR(dst_stat->st_mode)) return ErrorOf(std::errc::is_a_directory); // 同名のフォルダがある

            sink.file_started(src);
            std::int64_t counted = 0; // このファイルで、すでに足したバイト数
            auto copied = pl_copy_file(src, dst, on_conflict == OnConflict::Overwrite, [&](std::int64_t bytes) {
                if (bytes > counted) {
                    sink.bytes_done(bytes - counted);
                    counted = bytes;
                }
            });
            // 置き換えないとき、先に同名があると EEXIST になる。Skip なら、それが「スキップ」(調べてから置くまでの間に、同名ができた場合も)
            bool skipped = on_conflict == OnConflict::Skip && copied == std::errc::file_exists;
            if ((!copied || skipped) && S_ISREG(src_stat->st_mode) && src_stat->st_size > counted) {
                // 終えた(コピー・スキップ)ので、コールバックで足した分との差を、足す。クローンは、コールバックが来ない(0 から、ここで全部足す)。
                // スキップしたファイルも、終えたものとして数える(進捗が、100% まで進む)。失敗したファイルは、足さない
                sink.bytes_done(src_stat->st_size - counted);
            }
            if (skipped) return {};
            return copied;
        }

        // 1 項目の移動。成功(スキップを含む)なら nullopt、失敗なら、その理由。
        // unknown: 始める前の確認で、元か先を調べられなかった(上書きで先を消すのは、調べられたときだけ)。item_bytes: 見積もったバイト数
        std::optional<FileError> MoveEntry(
            const std::filesystem::path& src,
            const std::filesystem::path& dst,
            bool overwrite,
            bool unknown,
            std::int64_t item_bytes,
            const CopySink& sink
        )
        {
            std::error_code ec;
            if (ExistsNoFollow(dst)) {
                if (!overwrite) {
                    sink.bytes_done(item_bytes); // ユーザーがスキップを選択済み。終えたものとして数える。rename()の暗黙の上書きに頼らない
                    return std::nullopt;
                }
                if (unknown) {
                    // 元か先を調べられなかったので、先が元と同じものではないと言い切れない。消す前に諦める
                    return FileError{.message = "移動元か移動先を調べられないので、上書きしません"};
                }
                std::filesystem::remove_all(dst, ec);
                if (ec) return FileError::From(ec);
            }
            std::filesystem::rename(src, dst, ec);
            if (ec == std::errc::cross_device_link) {
                // 異なるボリューム間の move は rename できないため、コピー+削除にフォールバックする。ここへ来るとき、dst は無い(元からか、
                // 上書きで消した後)。同名があれば、同時にほかが作ったので、スキップせずに失敗にする(スキップを成功と数えると、
                // 置いていないファイルの元まで、下で消える)
                ec = CopyEntry(src, dst, OnConflict::Fail, sink);
                if (!ec) std::filesystem::remove_all(src, ec);
            }
            if (ec) return FileError::From(ec);
            return std::nullopt;
        }
    }

    bool ExistsNoFollow(const std::filesystem::path& path)
    {
        std::error_code ec;
        return std::filesystem::exists(std::filesystem::symlink_status(path, ec));
    }

    std::optional<std::filesystem::path> FindDuplicateName(const std::vector<std::filesystem::path>& sources)
    {
        std::unordered_set<std::string> seen;
        for (auto& src : sources) {
            if (!seen.insert(pl_name_collation_key(src.filename().string())).second) return src;
        }
        return std::nullopt;
    }

    std::int64_t MeasureCopyBytes(const std::filesystem::path& path)
    {
        struct stat st;
        if (::lstat(path.c_str(), &st) != 0) return 0;
        if (S_ISREG(st.st_mode)) return static_cast<std::int64_t>(st.st_size);
        if (!S_ISDIR(st.st_mode)) return 0; // リンク(たどらない)・特殊ファイル

        std::int64_t total = 0;
        std::error_code ec;
        std::filesystem::directory_iterator it(path, ec); // 開けない(権限が無い)ときは、ここで止まる(0 のまま。コピーの失敗で知らせる)
        for (; !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
            total += MeasureCopyBytes(it->path());
        }
        return total;
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
        const char* verb = FileOpLabel(type);

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
            return "移動先の同名のフォルダの中に移動元があるため、上書きできません";
        }
        return std::nullopt;
    }

    FileOperationManager::~FileOperationManager()
    {
        // アプリ終了時にまだ動いているスレッドが残っていると
        // std::thread のデストラクタが terminate() を呼ぶため、detach して安全に終了させる。
        // 状態(Job)はワーカーも shared_ptr で持っているので、ここで jobs_ が壊れても、ワーカーは破棄済みのものに触れない
        for (auto& entry : jobs_) {
            if (entry.thread.joinable()) {
                entry.thread.detach();
            }
        }
    }

    void FileOperationManager::SetCallback(std::function<void(FileOperationCompleted&)> callback)
    {
        callback_ = std::move(callback);
    }

    FileOperationManager::Job::Job(FileOperationId id, FileOpType type, std::int64_t total_items)
        : id(id), type(type), started_at(std::chrono::steady_clock::now())
    {
        progress_.total_items = total_items;
        progress_.phase = FileOperationProgress::Phase::Preparing; // 合計バイト数は、ワーカーが事前に走査して決める
    }

    void FileOperationManager::Job::SetTotals(std::int64_t total_bytes)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        progress_.total_bytes = total_bytes;
        progress_.phase = FileOperationProgress::Phase::Running;
    }

    void FileOperationManager::Job::SetCurrentName(std::string name)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        progress_.current_name = std::move(name);
    }

    void FileOperationManager::Job::AddBytes(std::int64_t bytes)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        progress_.done_bytes += bytes;
    }

    void FileOperationManager::Job::BeginItem(std::size_t index, std::string name)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        progress_.done_items = static_cast<std::int64_t>(index);
        progress_.current_name = std::move(name);
    }

    void FileOperationManager::Job::Complete(FileOperationCompleted completed)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        progress_.done_items = progress_.total_items;
        progress_.current_name.clear();
        result_ = std::move(completed);
    }

    FileOperationProgress FileOperationManager::Job::Snapshot() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return progress_;
    }

    std::optional<FileOperationCompleted> FileOperationManager::Job::TakeResult()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return std::exchange(result_, std::nullopt);
    }

    FileOperationId FileOperationManager::Start(
        FileOpType type,
        std::vector<std::filesystem::path> sources,
        std::filesystem::path dest_dir,
        bool overwrite
    )
    {
        auto job = std::make_shared<Job>(next_id_++, type, static_cast<std::int64_t>(sources.size()));
        std::thread thread(Run, job, std::move(sources), std::move(dest_dir), overwrite);
        jobs_.push_back(Entry{job, std::move(thread)});
        return job->id;
    }

    void FileOperationManager::Update()
    {
        // 1段目: 完了した操作の結果を取り出して、スレッドを join し、jobs_ から外す。jobs_ を触っている間は、コールバックを呼ばない
        std::vector<FileOperationCompleted> completed;
        for (auto it = jobs_.begin(); it != jobs_.end(); ) {
            auto result = it->job->TakeResult();
            if (!result) {
                ++it;
                continue;
            }
            it->thread.join();
            completed.push_back(std::move(*result));
            it = jobs_.erase(it);
        }

        // 2段目: 通知する。jobs_ は整合しているので、コールバックの中から Start() や Update() を呼んでもよい
        if (!callback_) return;
        for (auto& event : completed) {
            callback_(event);
        }
    }

    std::vector<FileOperationStatus> FileOperationManager::Running() const
    {
        std::vector<FileOperationStatus> statuses;
        statuses.reserve(jobs_.size());
        for (auto& entry : jobs_) {
            statuses.push_back(FileOperationStatus{
                .id = entry.job->id,
                .type = entry.job->type,
                .started_at = entry.job->started_at,
                .progress = entry.job->Snapshot(),
            });
        }
        return statuses;
    }

    void FileOperationManager::Run(
        std::shared_ptr<Job> job,
        std::vector<std::filesystem::path> sources,
        std::filesystem::path dest_dir,
        bool overwrite
    )
    {
        const FileOpType type = job->type;
        FileErrorSummary failures;
        FileOperationGuard guard(dest_dir);

        // 事前の走査: コピーするバイト数の合計(進捗の割合の分母)。同じボリュームへの移動は、rename で終わる(バイトを動かさない)ので、
        // 数えない。走査の間は「準備中」(Start が Preparing にしてある)
        std::vector<std::int64_t> item_bytes(sources.size(), 0);
        std::int64_t total_bytes = 0;
        for (std::size_t index = 0; index < sources.size(); ++index) {
            if (type == FileOpType::Move && SameVolume(sources[index], dest_dir)) continue;
            item_bytes[index] = MeasureCopyBytes(sources[index]);
            total_bytes += item_bytes[index];
        }
        job->SetTotals(total_bytes);

        CopySink sink{
            .file_started = [&job](const std::filesystem::path& file) { job->SetCurrentName(file.filename().string()); },
            .bytes_done = [&job](std::int64_t bytes) { job->AddBytes(bytes); },
        };

        // 先で同じ名前になる項目が、前にあれば、失敗にする(別々のフォルダの同名のファイルなど。上書きで前の項目を失う・
        // 黙ってスキップする、を避ける。始める前の確認(Application::CheckTransfer)で断っているが、多重の防御)
        std::unordered_set<std::string> placed;

        for (std::size_t index = 0; index < sources.size(); ++index) {
            auto& src = sources[index];
            auto dst = dest_dir / src.filename();
            // いま処理する項目(これより前の項目は、スキップや失敗も含めて、終えたものとして数える)
            job->BeginItem(index, src.filename().string());

            if (!placed.insert(pl_name_collation_key(src.filename().string())).second) {
                failures.Add(FileError{.message = "同じ名前の対象が、先にあります(先で重なります)"});
                continue;
            }

            // 始める前の確認(Application::CheckTransfer)と同じ確認を、コピー・消す直前にもやる(多重の防御)。
            // 確認してから、ここに順番が来るまでに、状況が変わっていることがある
            bool unknown = false;
            if (auto reason = guard.Check(type, src, unknown)) {
                failures.Add(FileError{.message = *reason});
                continue;
            }

            if (type == FileOpType::Copy) {
                if (auto ec = CopyEntry(src, dst, overwrite ? OnConflict::Overwrite : OnConflict::Skip, sink)) failures.Add(FileError::From(ec));
            }
            else if (auto failure = MoveEntry(src, dst, overwrite, unknown, item_bytes[index], sink)) {
                failures.Add(*failure);
            }
        }

        FileOperationCompleted completed{
            .id = job->id,
            .type = type,
            .success = failures.count == 0,
            .failed_count = failures.count,
            .error_message = failures.shown.message,
            .permission_denied = failures.shown.permission_denied,
            .dest_dir = dest_dir,
            .sources = std::move(sources),
        };

        job->Complete(std::move(completed));
    }
}

#ifndef FILE_OPERATION_H__
#define FILE_OPERATION_H__

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include "FileError.h"
#include "FileOperationProgress.h"
#include "models/FileListModel.h"

namespace miata {
    // FileOperationManager からコールバックで通知される完了イベント。
    // src_dir / dest_dir は操作開始時点でのペインの表示パスのスナップショット。
    // 完了時、該当ペインが今もこのパスを表示している場合のみ再スキャンするために使う
    // （バックグラウンド実行中に別ディレクトリへ移動された場合に誤って再スキャンしないため）。
    struct FileOperationCompleted {
        FileOperationId id; // Start() が返した ID
        FileOpType type;
        bool success;
        int failed_count;
        // 失敗の説明(最後の失敗。権限が無い失敗があれば、その説明)
        std::string error_message;
        // 失敗の中に、権限が無いもの(OSの保護など)があった。画面で、許可のしかたを案内する
        bool permission_denied;
        models::FileListModel* src_model;
        models::FileListModel* dest_model;
        std::filesystem::path src_dir;
        std::filesystem::path dest_dir;
        // 操作の対象にしたパス(開始時の、見えているマーク済みのファイル。無ければカーソル下の1件)。完了後に
        // 解除するマークを、そのペインの全マークではなく、これだけにするため(絞り込みで隠れているマークや、
        // 操作の最中に付けたマークは、解除しない)
        std::vector<std::filesystem::path> sources;
    };

    // 操作(コピー・移動)を始めてよいかの確認。元(src)と、先(dest_dir / src.filename())の関係が、データを失う・暴走する
    // 組み合わせのとき、断る理由を返す。画面で始める前(Application::StartFileOperation)と、裏スレッドが項目を処理する直前
    // (FileOperationManager::Run。外部の変更や、確認してから開始するまでの状況の変化に備える、多重の防御)の、両方で使う。
    // 断る組み合わせは、次の3つ:
    //   (1) 先が、元と同じ実体(同じフォルダへの操作)。Moveの「上書き」が、先 = 元を remove_all で消してしまう
    //   (2) 先のフォルダが、元のフォルダの中(またはそのもの)。フォルダを自分の中へ。Copyが、入れ子に増殖し続ける
    //   (3) 先の同名のフォルダが、元の祖先(/a/b/b を /a へ)。Moveの「上書き」が、/a/b を、元ごと丸ごと消す
    // 比べるのは、パスの文字列ではなく実体(st_dev, st_ino)。接頭辞の比較は foo と foobar を取り違え、シンボリックリンクや、
    // 大文字小文字を区別しないボリュームの別の綴りを見逃す。
    class FileOperationGuard {
    public:
        explicit FileOperationGuard(std::filesystem::path dest_dir);

        // srcへの操作を断る理由(「コピー先が、…」のような、画面に出す文)。断らなければ nullopt。
        // 元か先を調べられず、判断できなかったとき(statの失敗)は、unknown を true にして nullopt を返す。先を消す操作の前では、
        // 呼ぶ側が、これを「断る」側に倒す(調べられないまま消さない)
        std::optional<std::string> Check(FileOpType type, const std::filesystem::path& src, bool& unknown);

    private:
        using FileId = std::pair<std::uint64_t, std::uint64_t>; // (st_dev, st_ino)
        struct Chain {
            bool ok = false;
            std::vector<FileId> ids; // 自分と、ルートまでの祖先の実体(自分が先頭)
        };
        static Chain ChainOf(const std::filesystem::path& path);

        std::filesystem::path dest_dir_;
        Chain dest_chain_;
        // 元の親フォルダの鎖。操作の対象は同じフォルダにあるので、直近の1つを覚えて、項目ごとにやり直さない
        std::filesystem::path cached_parent_;
        Chain parent_chain_;
    };

    // ファイルのコピー・移動をバックグラウンドスレッドで実行する。
    // ワーカー(Run)は、ファイルI/Oと、自分の Job の進捗の更新だけを行い、Lua / View などのメインスレッド専用オブジェクトには、
    // 一切触れない。完了は Update() を呼んだスレッド(メインスレッド)上で、コールバックを通じて通知される。
    // Start / Update / Running / SetCallback は、メインスレッド専用(jobs_ に、ロックは無い)。
    class FileOperationManager {
    public:
        ~FileOperationManager();

        void SetCallback(std::function<void(FileOperationCompleted&)> callback);

        // 操作を、裏スレッドで始める。戻り値は、この操作の ID(Running() の写しと、完了のコールバックの id で、同じ操作と分かる)
        FileOperationId Start(
            FileOpType type,
            std::vector<std::filesystem::path> sources,
            std::filesystem::path src_dir,
            std::filesystem::path dest_dir,
            bool overwrite,
            models::FileListModel* src_model,
            models::FileListModel* dest_model
        );

        // Application::Update から毎ティック呼ぶ。完了した操作を拾って、jobs_ から外し、それから、コールバックで通知する。
        // 外してから通知するので、コールバックの中から Start() や Update() を呼んでもよい(jobs_ のイテレータが壊れず、
        // ネストした Update() が、同じ完了を二重に通知することもない)
        void Update();

        // 実行中の操作の、進み具合の写し(開始順)。ワーカーが終えていても、Update() が拾って通知するまでは含める
        // (除くと、拾う前に終えた操作が、通知もなく居なくなったように見える)
        std::vector<FileOperationStatus> Running() const;

    private:
        // ワーカー(Run)と共有する状態。shared_ptr で持つ: マネージャが先に破棄されても、動いているワーカーが、破棄済みの
        // Job に触れない。std::thread は、ここには入れない(ワーカー自身が最後の持ち主になって、動いているスレッドの
        // std::thread を壊す、を構造で避ける。下の Entry)
        struct Job {
            FileOperationId id = 0;
            FileOpType type = FileOpType::Copy;
            std::chrono::steady_clock::time_point started_at;
            mutable std::mutex mutex_; // progress_ と result_ を守る
            FileOperationProgress progress_;
            std::optional<FileOperationCompleted> result_;

            // ワーカー側
            void BeginItem(std::size_t index, std::string name); // index 件目の項目を始める(done_items = index)
            void Complete(FileOperationCompleted completed);      // 全部終えた(done_items = total)。結果を置く
            // メインスレッド側
            FileOperationProgress Snapshot() const;
        };
        // マネージャが持つ 1 操作
        struct Entry {
            std::shared_ptr<Job> job;
            std::thread thread;
        };

        static void Run(
            std::shared_ptr<Job> job,
            FileOpType type,
            std::vector<std::filesystem::path> sources,
            std::filesystem::path src_dir,
            std::filesystem::path dest_dir,
            bool overwrite,
            models::FileListModel* src_model,
            models::FileListModel* dest_model
        );

        std::function<void(FileOperationCompleted&)> callback_;
        std::vector<Entry> jobs_;
        FileOperationId next_id_ = 1;
    };
}

#endif // FILE_OPERATION_H__

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

namespace miata {
    // FileOperationManager からコールバックで通知される完了イベント。
    // 完了後に、どのペインを再スキャン・マーク解除するかは、ペインのポインタではなく、パス(dest_dir と sources の親フォルダ)で
    // 引く(Application::OnFileOperationCompleted): 操作の対象と宛先は、Lua から渡された任意のパスで、バックグラウンド実行中に、
    // ペインが別のフォルダへ移っていても、いま、そのフォルダを表示しているペインだけを更新するため。
    struct FileOperationCompleted {
        FileOperationId id; // Start() が返した ID
        FileOpType type;
        bool success;
        int failed_count;
        // 失敗の説明(最後の失敗。権限が無い失敗があれば、その説明)
        std::string error_message;
        // 失敗の中に、権限が無いもの(OSの保護など)があった。画面で、許可のしかたを案内する
        bool permission_denied;
        // 宛先のフォルダ(開始時)
        std::filesystem::path dest_dir;
        // 操作の対象にしたパス(開始時)。完了後に解除するマークを、そのペインの全マークではなく、これだけにするため
        // (絞り込みで隠れているマークや、操作の最中に付けたマークは、解除しない)。元のフォルダは、これの親
        std::vector<std::filesystem::path> sources;
    };

    // 操作(コピー・移動)を始めてよいかの確認。元(src)と、先(dest_dir / src.filename())の関係が、データを失う・暴走する
    // 組み合わせのとき、断る理由を返す。画面で始める前(Application::CheckTransfer)と、裏スレッドが項目を処理する直前
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

    // リンクそのもの(たどらない)で、path があるか。壊れたシンボリックリンクも「ある」(std::filesystem::exists は、リンクをたどるので「無い」)。
    // 開始前の同名の数え上げ(Application::CheckTransfer)と、移動の「先が既にあるか」が使う。コピー(CopyEntry)は、lstat で同じ見方をする
    bool ExistsNoFollow(const std::filesystem::path& path);

    // 対象(sources)の中に、先で同じ名前になるものがあるか。名前(filename())を pl_name_collation_key で比べる: 同じ名前
    // (別々のフォルダの README.md など)・大文字小文字だけが違う名前・正規化(NFC/NFD)だけが違う名前は、大文字小文字や正規化を
    // 区別しないボリューム(APFSの既定)では、先で同じ名前になる。コピーでは、2つ目が、1つ目を上書きする(上書きのとき)か、黙って
    // スキップされ、移動では、上書きで、1つ目が失われる。あれば、重なっている2つ目以降のパスを返す(無ければ nullopt)。
    // Lua から、別々のフォルダの対象をまとめて渡せるので、始める前の確認(Application::CheckTransfer)で断る。Run も、項目ごとに
    // 同じ見方で、2つ目以降を失敗にする(多重の防御)
    std::optional<std::filesystem::path> FindDuplicateName(const std::vector<std::filesystem::path>& sources);

    // コピーするバイト数の見積り(進捗の割合の分母にする、事前の走査。テストも直接呼ぶ)。path が普通のファイルなら、その大きさ。フォルダなら、中にある
    // 普通のファイルの大きさの合計(再帰)。リンク(たどらない)と、特殊ファイル(FIFO・ソケット・デバイス)は 0。調べられないもの
    // (権限が無い・走査の途中で消えた)も 0 として数える(コピーの失敗で、改めて知らせる)
    std::int64_t MeasureCopyBytes(const std::filesystem::path& path);

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
            std::filesystem::path dest_dir,
            bool overwrite
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
            // 作るのはメインスレッド(Start)で、スレッドを起こす前。id・type・started_at は、以降変わらない(ロック不要)。
            // 合計の項目数も、このとき決める。合計のバイト数は、ワーカーが事前に走査して決める(それまでは「準備中」)
            Job(FileOperationId id, FileOpType type, std::int64_t total_items);

            const FileOperationId id;
            const FileOpType type;
            const std::chrono::steady_clock::time_point started_at;
            mutable std::mutex mutex_; // progress_ と result_ を守る
            FileOperationProgress progress_;
            std::optional<FileOperationCompleted> result_;

            // ワーカー側
            void SetTotals(std::int64_t total_bytes);              // 事前の走査が終わった: 合計を入れて、Running にする
            void BeginItem(std::size_t index, std::string name);   // index 件目の項目を始める(done_items = index。名前は、その項目の名前)
            void SetCurrentName(std::string name);                  // いま写しているファイルの名前(項目の中の、葉)
            void AddBytes(std::int64_t bytes);                      // 終えた(コピーした・スキップした)バイト数を足す
            void Complete(FileOperationCompleted completed);        // 全部終えた(done_items = total)。結果を置く
            // メインスレッド側
            FileOperationProgress Snapshot() const;
            std::optional<FileOperationCompleted> TakeResult(); // 結果が置かれていれば、取り出す(1 回だけ)
        };
        // マネージャが持つ 1 操作
        struct Entry {
            std::shared_ptr<Job> job;
            std::thread thread;
        };

        static void Run(
            std::shared_ptr<Job> job,
            std::vector<std::filesystem::path> sources,
            std::filesystem::path dest_dir,
            bool overwrite
        );

        std::function<void(FileOperationCompleted&)> callback_;
        std::vector<Entry> jobs_;
        FileOperationId next_id_ = 1;
    };
}

#endif // FILE_OPERATION_H__

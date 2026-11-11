#ifndef SHELL_JOB_H__
#define SHELL_JOB_H__

#include <cstddef>
#include <expected>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "models/ConsoleLog.h"

namespace miata {
    // 起動したコマンド(Miata.command.shell)の標準出力・標準エラーを、コンソールの受け口(ConsoleLog)に流す。
    // スレッドは使わない: 出力のパイプ(非ブロッキング)を、Update() のたびに、読める分だけ読む(Application::Update が、毎ティック呼ぶ)。
    //  - 標準出力の行は Normal、標準エラーの行は Error(LineAssembler で行にして、ConsoleLog::AppendLine に足す)
    //  - 1 回の Update で読むのは、出力 1 本につき kReadBudget バイトまで。読まなければ、パイプが詰まって、子の書き込みが待つので、
    //    大量に出すコマンドでも、1 回の Update の時間と、使うメモリは、増えない
    //  - 終了(on_exit)は、子が終わるまでに書いた出力を、読んでから知らせる(終了の知らせは、その出力の行より後になる)
    //  - 子が終わっても、出力の口を握っているプロセス(裏で動かした孫)が居る間は、読み続ける(その出力も流れる)
    // 止める手段と、期限は、無い。全部、メインスレッド専用(ロックは無い)。
    class ShellJobManager {
    public:
        // 1 回の Update で、出力 1 本から読む量の上限(バイト)。短い行ばかりを出し続けるコマンド(yes)でも、1 回の Update が、
        // ティック(0.05 秒)に比べて、十分短く済む大きさにしてある(行ごとに、整形と、受け口への追加がある)
        static constexpr size_t kReadBudget = 32 * 1024;
        // 子が終わった後、出力 1 本から、これだけ読んでも空にならなければ、読み切るのを待たずに、終了を知らせる
        // (出力の口を握った孫が、書き続けている。子が残した出力は、多くてもパイプの容量までで、これより少ない)
        static constexpr size_t kPostExitBudget = 256 * 1024;

        // 子が終わったときに、1 回だけ呼ばれる(Update の中から。メインスレッド)。exit_code は終了コード
        // (シグナルで終わったときは 128 + シグナル番号。終了を調べられなかったときは 0)。
        // 呼ばれる時点で、そのコマンドの片付けは済んでいるので、中から Start を呼んでもよい
        using ExitCallback = std::function<void(int exit_code)>;

        explicit ShellJobManager(models::ConsoleLog& log);
        // 読み口を閉じる(動いている子は、止めない。次の書き込みで、SIGPIPE を受ける)
        ~ShellJobManager();
        ShellJobManager(const ShellJobManager&) = delete;
        ShellJobManager& operator=(const ShellJobManager&) = delete;

        // command(/bin/sh -c のコマンドライン。arguments は位置パラメータ)を、待たずに起動して、出力を流し始める
        // (pl_spawn_shell_piped)。起動できなければ、その理由(on_exit は、呼ばれない)
        std::expected<void, std::string> Start(
            const std::string& command, const std::vector<std::string>& arguments, ExitCallback on_exit);

        // 毎ティック呼ぶ。終わった子を回収し、出力を読んで、行を受け口に足し、終了を知らせる
        void Update();

        // 抱えているコマンドの数(終わっていないもの、と、終わったが、出力の口を握った孫が残っているもの)
        size_t Running() const { return jobs_.size(); }

    private:
        struct Job; // 起動した 1 つのコマンド(ShellJob.cc)

        models::ConsoleLog& log_;
        std::vector<std::unique_ptr<Job>> jobs_;
    };
}

#endif // SHELL_JOB_H__

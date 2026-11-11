#include "ShellJob.h"
#include <sys/wait.h>
#include <unistd.h>
#include <algorithm>
#include <array>
#include <cerrno>
#include <utility>
#include "LineAssembler.h"
#include "platform.h"

namespace miata {
    namespace {
        // 出力 1 本(標準出力か、標準エラー)。読み口を持つ(破棄すると、閉じる)
        struct Stream {
            Stream(int fd, models::LogKind kind, models::ConsoleLog& log)
                : fd(fd)
                , lines(models::ConsoleLog::kMaxLineBytes)
                , emit([&log, kind](std::string_view line, bool truncated) { log.AppendLine(kind, line, truncated); })
            {
            }
            ~Stream()
            {
                if (fd >= 0) close(fd);
            }
            Stream(const Stream&) = delete;
            Stream& operator=(const Stream&) = delete;

            int fd;                         // 読み口。閉じたら -1
            LineAssembler lines;
            LineAssembler::Emit emit;       // 組み上がった行を、受け口に足す(標準出力は Normal、標準エラーは Error)
            size_t read_after_exit = 0;     // 子の終了を回収した後に、読んだ量
            bool empty_after_exit = false;  // 子の終了を回収した後に、空(EAGAIN)を見た

            // 持ち越している行(改行の無い最後の行)を出す
            void Flush()
            {
                lines.Flush(emit);
            }

            void Close()
            {
                Flush();
                close(fd);
                fd = -1;
            }

            // 読める分を読んで、行を受け口に足す。1 回に読むのは kReadBudget バイトまで(残りは、次の Update)。
            // exited は、子の終了を、もう回収したか
            void Read(bool exited)
            {
                std::array<char, 16 * 1024> buffer;
                size_t budget = ShellJobManager::kReadBudget;
                while (fd >= 0 && budget > 0) {
                    const ssize_t n = read(fd, buffer.data(), std::min(buffer.size(), budget));
                    if (n > 0) {
                        const auto size = static_cast<size_t>(n);
                        lines.Feed(std::string_view(buffer.data(), size), emit);
                        budget -= size;
                        if (exited) read_after_exit += size;
                    }
                    else if (n == 0) {
                        Close(); // EOF: 書き口を持つプロセスが、もう居ない
                    }
                    else if (errno == EAGAIN) {
                        if (exited) empty_after_exit = true;
                        return; // いまは空
                    }
                    else if (errno != EINTR) {
                        Close(); // 読めないエラー。EOF と同じ扱い
                    }
                }
            }

            // 子が終わるまでに書いた出力を、読み終えた: 閉じた(EOF)か、終了を回収した後に、空を見た。
            // 空にならないまま、たくさん読んだときも、待たない(出力の口を握った孫が、書き続けている)
            bool Settled() const
            {
                return fd < 0 || empty_after_exit || read_after_exit >= ShellJobManager::kPostExitBudget;
            }
        };

        // waitpid の status を、終了コードにする(pl_spawn_shell_command と同じ: シグナルで終わったら、128 + シグナル番号)
        int ExitCodeOf(int status)
        {
            if (WIFEXITED(status)) return WEXITSTATUS(status);
            return 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
        }
    }

    struct ShellJobManager::Job {
        Job(const PipedProcess& process, models::ConsoleLog& log, ExitCallback on_exit)
            : pid(process.pid)
            , on_exit(std::move(on_exit))
            , out(process.stdout_fd, models::LogKind::Normal, log)
            , err(process.stderr_fd, models::LogKind::Error, log)
        {
        }

        pid_t pid;
        bool exited = false;   // 終了を回収した(waitpid)
        int exit_code = 0;
        bool reported = false; // 終了を知らせた
        ExitCallback on_exit;
        Stream out;
        Stream err;
    };

    ShellJobManager::ShellJobManager(models::ConsoleLog& log) : log_(log)
    {
    }

    // 残っているコマンドの読み口は、Stream が閉じる
    ShellJobManager::~ShellJobManager() = default;

    std::expected<void, std::string> ShellJobManager::Start(
        const std::string& command, const std::vector<std::string>& arguments, ExitCallback on_exit)
    {
        const auto process = pl_spawn_shell_piped(command, arguments);
        if (!process) return std::unexpected(process.error());
        jobs_.push_back(std::make_unique<Job>(*process, log_, std::move(on_exit)));
        return {};
    }

    void ShellJobManager::Update()
    {
        // 終了の知らせは、下のループと、jobs_ から外す処理が済んでから呼ぶ: 中から Start が呼ばれても、走査中の jobs_ が、変わらない
        std::vector<std::pair<ExitCallback, int>> exits;

        for (auto& job : jobs_) {
            // 先に、終了を調べる。終わっていれば、子が書いた出力は、もう全部、パイプの中にあるので、この後の読み取りが
            // 「終了の後の読み取り」になり、同じ Update の中で、読み終えたと分かる(Stream::Settled。「終了の後に空を見た」の印を
            // 立てるのは、終了を回収した後の読み取りだけ。終了の前の読み取りで立てると、読み残したまま、終了を知らせてしまう)
            if (!job->exited) {
                int status = 0;
                const pid_t reaped = waitpid(job->pid, &status, WNOHANG);
                if (reaped == job->pid) {
                    job->exited = true;
                    job->exit_code = ExitCodeOf(status);
                }
                else if (reaped < 0 && errno != EINTR) {
                    // 回収できない(SIGCHLD を無視している、など)。終わったものとして、終了コードは 0(pl_spawn_shell_command と同じ)
                    job->exited = true;
                    job->exit_code = 0;
                }
            }

            job->out.Read(job->exited);
            job->err.Read(job->exited);

            if (job->exited && !job->reported && job->out.Settled() && job->err.Settled()) {
                // 改行の無い最後の行も、終了の知らせより前に出す(出力の口が、まだ開いていても)
                job->out.Flush();
                job->err.Flush();
                job->reported = true;
                exits.emplace_back(std::move(job->on_exit), job->exit_code);
            }
        }

        // 終了を知らせて、出力の口も全部閉じたものを、外す(口を握った孫が居る間は、残して、読み続ける)
        std::erase_if(jobs_, [](const std::unique_ptr<Job>& job) {
            return job->reported && job->out.fd < 0 && job->err.fd < 0;
        });

        for (auto& [on_exit, exit_code] : exits) {
            if (on_exit) on_exit(exit_code);
        }
    }
}

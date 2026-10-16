#include "StdioCapture.h"
#include <cstdio>
#include <string_view>

namespace miata {
    namespace {
        // funopen の cookie。差し替えた FILE が生きている間(プロセスの終わりまで)必要なので、解放しない
        struct Stream {
            models::ConsoleLog* log;
            models::LogKind kind;
            FILE* orig; // 差し替える前の FILE(書き戻し先)
        };

        bool g_installed = false;

        // 差し替えた FILE に書かれたバイト列を受ける。FILE のロックを持ったまま、書いたスレッドで呼ばれる(StdioCapture.h)。
        // Write は例外を投げない。書き戻しは、ConsoleLog のロックの外
        int WriteToLog(void* cookie, const char* buf, int n)
        {
            if (n <= 0) return n;
            const auto* stream = static_cast<const Stream*>(cookie);
            stream->log->Write(stream->kind, std::string_view(buf, static_cast<size_t>(n)));
            // 書き戻す。毎回 flush する: exit() の flush は、FILE の並びの順に走るので、差し替えた FILE の最後の未完の行
            // (改行の無い出力)は、元の FILE が先に flush された後に届きうる。flush しないと、その分が失われる
            std::fwrite(buf, 1, static_cast<size_t>(n), stream->orig);
            std::fflush(stream->orig);
            return n;
        }
    }

    bool StdioCapture::Install(models::ConsoleLog& log)
    {
        if (g_installed) return true;

        FILE* const orig_out = stdout;
        FILE* const orig_err = stderr;
        auto* out_stream = new Stream{&log, models::LogKind::Normal, orig_out};
        auto* err_stream = new Stream{&log, models::LogKind::Error, orig_err};
        FILE* out = funopen(out_stream, nullptr, WriteToLog, nullptr, nullptr);
        FILE* err = funopen(err_stream, nullptr, WriteToLog, nullptr, nullptr);
        if (out == nullptr || err == nullptr) {
            if (out != nullptr) std::fclose(out);
            if (err != nullptr) std::fclose(err);
            delete out_stream;
            delete err_stream;
            return false;
        }
        // どちらも行バッファ(改行か fflush で、コールバックに届く)。stderr をバッファなし(_IONBF)にしてはいけない:
        // fprintf(stderr, "T%d-%05d\n", ...) が、書式の部品ごと("T"・"1"・"-"…)に、別々のコールバックに分かれて届き、
        // 別スレッドの標準出力の行が間に割り込むと、書きかけの行が壊れる(ConsoleLog::Write は、他方の種類の未完の行を、
        // 先に 1 行にするため)。行バッファなら、1 回の出力が、FILE のロックの下で 1 つにまとまって届く(バッファに収まる範囲。
        // macOS の BUFSIZ は 1024 バイトで、それを超える出力は、複数のコールバックに分かれて届く。書くスレッドが 1 本の間は問題ない)
        std::setvbuf(out, nullptr, _IOLBF, 0);
        std::setvbuf(err, nullptr, _IOLBF, 0);
        stdout = out;
        stderr = err;
        g_installed = true;
        return true;
    }

    bool StdioCapture::IsInstalled()
    {
        return g_installed;
    }
}

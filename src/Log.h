#ifndef LOG_H__
#define LOG_H__

#include <cstdio>
#include <string>
#include <string_view>

namespace miata {
    // 診断(エラー・警告)を 1 行(複数行でもよい)出す、唯一の入口。標準エラーへ、末尾に改行を付けて、1 回の fwrite で書く
    // (他の出力と混ざらないよう、行を組んでから書く)。
    // 標準エラーは StdioCapture が捕まえるので、コンソールには赤い行として出て、ターミナルから起動したときは、ターミナルにも出る。
    // 受け口(ConsoleLog)を知らずに使える。Lua のエラーも、ファイル操作の失敗も、これを通す: printf で標準出力に出すと、
    // 通常の(赤くない)行になる。message の末尾の改行は 1 つまで取り込む(空行を増やさない)
    inline void LogError(std::string_view message)
    {
        std::string line;
        line.reserve(message.size() + 1);
        line.append(message);
        if (line.empty() || line.back() != '\n') line.push_back('\n');
        std::fwrite(line.data(), 1, line.size(), stderr);
    }
}

#endif // LOG_H__

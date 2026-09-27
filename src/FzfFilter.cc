#include "FzfFilter.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <fstream>
#include <unistd.h>

#include "platform.h"

namespace miata {
    namespace {
        std::string ToLower(const std::string& s)
        {
            std::string r = s;
            std::transform(r.begin(), r.end(), r.begin(), [](unsigned char c) {
                return (char)std::tolower(c);
            });
            return r;
        }

        std::vector<std::string> SplitLines(const std::string& text)
        {
            std::vector<std::string> lines;
            size_t start = 0;
            while (start <= text.size()) {
                size_t end = text.find('\n', start);
                if (end == std::string::npos) end = text.size();
                if (end > start) lines.push_back(text.substr(start, end - start));
                start = end + 1;
            }
            return lines;
        }
    }

    FzfFilter::FzfFilter(std::vector<std::string> candidates) : candidates_(std::move(candidates))
    {
        fzf_path_ = pl_find_executable("fzf");
        if (!fzf_path_) return;

        auto path = std::filesystem::temp_directory_path() /
            std::format("miata-fzf-{}.txt", (long)getpid());

        std::ofstream out(path, std::ios::binary);
        if (!out) {
            fzf_path_ = std::nullopt; // 書き出せなければ以後フォールバックに徹する
            return;
        }
        for (auto& c : candidates_) {
            out << c << '\n';
        }
        out.close();
        temp_file_ = path;
    }

    FzfFilter::~FzfFilter()
    {
        if (temp_file_) {
            std::error_code ec;
            std::filesystem::remove(*temp_file_, ec);
        }
    }

    std::vector<std::string> FzfFilter::Filter(const std::string& query) const
    {
        if (query.empty()) return candidates_;
        if (!fzf_path_ || !temp_file_) return SubstringFallback(query);

        auto result = pl_run_process(*fzf_path_, { "--filter", query }, *temp_file_);
        if (!result) return SubstringFallback(query); // 起動そのものに失敗

        // fzf --filter はマッチが1件も無い場合に終了コード1を返すが、これは正当な「該当なし」であって
        // 起動失敗ではない。exit_codeでフォールバックへ分岐したりはしない。
        return SplitLines(result->stdout_text);
    }

    std::vector<std::string> FzfFilter::SubstringFallback(const std::string& query) const
    {
        auto needle = ToLower(query);
        std::vector<std::string> result;
        for (auto& c : candidates_) {
            if (ToLower(c).find(needle) != std::string::npos) {
                result.push_back(c);
            }
        }
        return result;
    }
}

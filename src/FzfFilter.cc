#include "FzfFilter.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <format>
#include <fstream>
#include <string_view>
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

        // 一時ファイルの名前は、FzfFilterごとに別にする(ダイアログと、左右のペインが、同時に持つため)。
        // 一時フォルダが無い(TMPDIRが壊れている等)ときは、例外ではなくnulloptを返す(呼び出し側はフォールバックする)
        std::optional<std::filesystem::path> NextTempPath()
        {
            static std::atomic<unsigned> counter{0};
            std::error_code ec;
            auto dir = std::filesystem::temp_directory_path(ec);
            if (ec) return std::nullopt;
            return dir / std::format("miata-fzf-{}-{}.txt", (long)getpid(), counter++);
        }
    }

    FzfFilter::FzfFilter(std::vector<std::string> candidates) : FzfFilter(std::move(candidates), pl_find_executable("fzf"))
    {
    }

    FzfFilter::FzfFilter(std::vector<std::string> candidates, std::optional<std::filesystem::path> fzf_path)
        : candidates_(std::move(candidates)), fzf_path_(std::move(fzf_path))
    {
        for (auto& c : candidates_) {
            std::replace(c.begin(), c.end(), '\n', ' ');
            std::replace(c.begin(), c.end(), '\r', ' ');
        }
        for (size_t i = 0; i < candidates_.size(); ++i) {
            indices_of_[candidates_[i]].push_back(i);
        }
        if (fzf_path_) WriteCandidates();
    }

    void FzfFilter::WriteCandidates()
    {
        // 書き出せなければ、temp_file_を設定しない: 以後、UsesFzf()がfalseになり、フォールバックに徹する
        auto temp_path = NextTempPath();
        if (!temp_path) return;
        auto path = *temp_path;
        std::ofstream out(path, std::ios::binary);
        for (auto& c : candidates_) {
            out << c << '\n';
        }
        out.close();
        if (!out) { // 開けなかった・書けなかった(開けなければ、失敗の状態が最後まで残る)
            std::error_code ec;
            std::filesystem::remove(path, ec);
            return;
        }
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
        if (!UsesFzf()) return SubstringFallback(query);

        auto result = pl_run_process(*fzf_path_, { "--filter", query }, *temp_file_);
        if (!result) return SubstringFallback(query); // 起動そのものに失敗(期限内に終わらなかった場合も)

        // fzf --filter はマッチが1件も無い場合に終了コード1を返すが、これは正当な「該当なし」であって
        // 起動失敗ではない。exit_codeでフォールバックへ分岐したりはしない。
        return SplitLines(result->stdout_text);
    }

    std::vector<size_t> FzfFilter::FilterIndices(const std::string& query) const
    {
        std::vector<size_t> indices;
        // 出力の行を、候補の添字に戻す。同じ文字列の行は、その候補の順に1件ずつ使う(語が空なら、Filter()が候補をそのまま
        // 返すので、全部の添字が昇順で返る)
        std::unordered_map<std::string_view, size_t> used;
        for (auto& line : Filter(query)) {
            auto found = indices_of_.find(line);
            if (found == indices_of_.end()) continue; // 候補に無い行(fzfが候補を書き換えた等)は捨てる
            size_t& next = used[found->first];
            if (next >= found->second.size()) continue;
            indices.push_back(found->second[next++]);
        }
        return indices;
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

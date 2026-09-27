#ifndef FZF_FILTER_H__
#define FZF_FILTER_H__

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace miata {
    // 文字列一覧に対してfzf(--filter)による絞り込みを行う。AppKitに依存しない。
    // 候補一覧はコンストラクタで一時ファイルに書き出し、Filter()はクエリ毎にfzfを起動して
    // 絞り込み結果を得る(候補が変わらないダイアログ1セッション分の利用を想定している)。
    // fzfが見つからない、または起動に失敗した場合は大文字小文字を無視した部分一致にフォールバックする。
    class FzfFilter {
    public:
        explicit FzfFilter(std::vector<std::string> candidates);
        ~FzfFilter();

        FzfFilter(const FzfFilter&) = delete;
        FzfFilter& operator=(const FzfFilter&) = delete;

        // queryが空なら候補をそのまま(元の順序で)返す。
        std::vector<std::string> Filter(const std::string& query) const;

    private:
        std::vector<std::string> SubstringFallback(const std::string& query) const;

        std::vector<std::string> candidates_;
        std::optional<std::filesystem::path> fzf_path_;
        std::optional<std::filesystem::path> temp_file_;
    };
}

#endif // FZF_FILTER_H__

#ifndef FZF_FILTER_H__
#define FZF_FILTER_H__

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace miata {
    // 文字列一覧に対してfzf(--filter)による絞り込みを行う。AppKitに依存しない。
    // 候補一覧はコンストラクタで一時ファイルに書き出し、Filter()はクエリ毎にfzfを起動して
    // 絞り込み結果を得る(候補が変わらない間の利用を想定している: ダイアログ1セッション、ディレクトリの1回の走査)。
    // 結果は、fzfが出力した順(得点順。得点が同じなら、fzfの既定の並び=短い候補が先、同じ長さなら候補の順)。
    // クエリはfzfの拡張検索の構文のまま渡る(空白区切りでAND、^先頭一致、末尾$、'完全一致、!否定、|OR)。
    // fzfが見つからない、または起動に失敗した場合は大文字小文字を無視した部分一致(候補の順)にフォールバックする。
    // 一致した位置は分からない(fzfの--filterは位置を出力しない)。
    class FzfFilter {
    public:
        // 改行(\n \r)を含む候補は、空白に置き換える(fzfは行単位で読むため)。置き換えた後の文字列が、Filter()が返す文字列になる
        explicit FzfFilter(std::vector<std::string> candidates);
        // fzf_pathを指定する(テスト用。nulloptならfzfなし=フォールバックに徹する)
        FzfFilter(std::vector<std::string> candidates, std::optional<std::filesystem::path> fzf_path);
        ~FzfFilter();

        FzfFilter(const FzfFilter&) = delete;
        FzfFilter& operator=(const FzfFilter&) = delete;

        // queryが空なら候補をそのまま(元の順序で)返す。
        std::vector<std::string> Filter(const std::string& query) const;
        // Filter()と同じ絞り込みの結果を、候補の添字で返す(同じ順)。同じ文字列の候補が複数あっても、1件ずつ別に返す
        // (同じ文字列は得点も同じなので、fzfが候補の順に出力する)。候補に無い行と、候補より多い同じ行は捨てる。
        // queryが空なら、全部の添字を昇順で返す。
        std::vector<size_t> FilterIndices(const std::string& query) const;
        // fzfで絞り込んでいるか(falseなら、部分一致にフォールバックしている)
        bool UsesFzf() const { return fzf_path_ && temp_file_; }

    private:
        void WriteCandidates();
        std::vector<std::string> SubstringFallback(const std::string& query) const;

        std::vector<std::string> candidates_;
        std::optional<std::filesystem::path> fzf_path_;
        std::optional<std::filesystem::path> temp_file_;
        // 候補の文字列 → その添字(昇順)。FilterIndices()が、出力の行を添字に戻すのに使う
        std::unordered_map<std::string, std::vector<size_t>> indices_of_;
    };
}

#endif // FZF_FILTER_H__

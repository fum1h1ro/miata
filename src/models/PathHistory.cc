#include "PathHistory.h"
#include <unordered_set>

namespace miata::models {
    std::string PathHistory::TrimTrailingSlash(std::string path)
    {
        while (path.size() > 1 && path.back() == '/') path.pop_back();
        return path;
    }

    std::optional<std::string> PathHistory::Key(const std::filesystem::path& dir)
    {
        std::string s = dir.string();
        if (s.empty() || s[0] != '/') return std::nullopt;
        if (s.find_first_of("\n\r\0", 0, 3) != std::string::npos) return std::nullopt; // "\0" も含めるため長さを渡す
        return TrimTrailingSlash(std::move(s));
    }

    bool PathHistory::Trim()
    {
        if (entries_.size() <= limit_) return false;
        entries_.resize(limit_);
        return true;
    }

    void PathHistory::SetLimit(size_t limit)
    {
        limit_ = limit;
        if (Trim()) dirty_ = true;
    }

    void PathHistory::Record(const std::filesystem::path& dir)
    {
        if (limit_ == 0) return;
        auto key = Key(dir);
        if (!key) return;
        if (!entries_.empty() && entries_.front() == *key) return; // すでに先頭。変えない(保存も要らない)

        std::erase(entries_, *key); // すでにあれば外す(重複しない)
        entries_.insert(entries_.begin(), std::move(*key));
        Trim();
        dirty_ = true;
    }

    bool PathHistory::Remove(const std::filesystem::path& dir)
    {
        auto key = Key(dir);
        if (!key || std::erase(entries_, *key) == 0) return false;
        dirty_ = true;
        return true;
    }

    std::vector<std::string> PathHistory::List(const std::filesystem::path& current) const
    {
        auto result = entries_;
        if (auto exclude = Key(current)) std::erase(result, *exclude);
        return result;
    }

    void PathHistory::Restore(const std::vector<std::string>& saved)
    {
        std::vector<std::string> restored;
        std::unordered_set<std::string> seen;
        for (auto& item : saved) {
            if (restored.size() >= limit_) break;
            auto key = Key(item);
            if (!key || !seen.insert(*key).second) continue;
            restored.push_back(std::move(*key));
        }
        dirty_ = (restored != saved);
        entries_ = std::move(restored);
    }
}

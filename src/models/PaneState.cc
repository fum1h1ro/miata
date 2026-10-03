#include "PaneState.h"
#include "PathHistory.h"

namespace miata::models {
    bool PaneState::IsPlainAbsolutePath(std::string_view path)
    {
        if (path.empty() || path[0] != '/') return false;
        if (path.find('\0') != std::string_view::npos) return false;
        while (path.size() > 1 && path.back() == '/') path.remove_suffix(1);
        if (path.size() == 1) return true; // "/"
        path.remove_prefix(1);
        while (true) {
            auto slash = path.find('/');
            auto element = path.substr(0, slash);
            if (element.empty() || element == "." || element == "..") return false;
            if (slash == std::string_view::npos) return true;
            path.remove_prefix(slash + 1);
        }
    }

    std::map<std::string, std::string> PaneState::ToMap() const
    {
        return {
            {"path", path},
            {"sort", sort_key},
            {"reverse", sort_reverse ? "1" : "0"},
        };
    }

    PaneState PaneState::FromMap(const std::map<std::string, std::string>& saved)
    {
        PaneState state;
        if (auto it = saved.find("path"); it != saved.end() && IsPlainAbsolutePath(it->second)) {
            state.path = PathHistory::TrimTrailingSlash(it->second); // 履歴や、画面に出す形と同じ(末尾の/なし)にそろえる
        }
        if (auto it = saved.find("sort"); it != saved.end() && !it->second.empty()) state.sort_key = it->second;
        if (auto it = saved.find("reverse"); it != saved.end()) state.sort_reverse = (it->second == "1");
        return state;
    }
}

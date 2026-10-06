#include "FilterState.h"

#include <algorithm>
#include <format>

namespace miata::views {

std::string FilterCountText(const FilterStatus& status)
{
    if (status.mode == QueryMode::Idle || status.query.empty()) return "";

    std::string text = status.shown == 0 ? "見つかりません" : std::format("{}/{}", status.shown, status.total);
    if (status.hidden_marks > 0) text += std::format("  隠れたマーク {}", status.hidden_marks);
    if (status.fuzzy_fallback) text += "  fzfなし(部分一致)";
    return text;
}

const char* MatchKindName(MatchKind kind)
{
    switch (kind) {
    case MatchKind::Substring: return "substring";
    case MatchKind::Fuzzy: return "fuzzy";
    }
    return "";
}

std::optional<MatchKind> ParseMatchKind(std::string_view name)
{
    if (name == "substring") return MatchKind::Substring;
    if (name == "fuzzy") return MatchKind::Fuzzy;
    return std::nullopt;
}

int NearestShownRow(const std::vector<int>& row_of_sorted, int pivot)
{
    const int size = static_cast<int>(row_of_sorted.size());
    if (size == 0) return 0;

    pivot = std::clamp(pivot, 0, size - 1);
    for (int i = pivot; i < size; ++i) {
        if (row_of_sorted[static_cast<size_t>(i)] >= 0) return row_of_sorted[static_cast<size_t>(i)];
    }
    for (int i = pivot - 1; i >= 0; --i) {
        if (row_of_sorted[static_cast<size_t>(i)] >= 0) return row_of_sorted[static_cast<size_t>(i)];
    }
    return 0;
}

void FilterState::Begin(ListPosition origin, MatchKind kind)
{
    if (mode_ == QueryMode::Typing) return;

    // 確定済みの絞り込みがあれば、Escで戻れるよう、語と一致のしかたを取っておく
    saved_ = mode_ == QueryMode::Committed ? std::optional<Saved>(Saved{query_, kind_}) : std::nullopt;
    mode_ = QueryMode::Typing;
    kind_ = kind;
    query_.clear();
    row_ranges_.clear();
    origin_ = origin;
    anchor_ = std::move(origin);
}

void FilterState::SetQuery(std::string query)
{
    if (mode_ != QueryMode::Typing) return;
    query_ = std::move(query);
    row_ranges_.clear();
}

void FilterState::Commit()
{
    if (mode_ != QueryMode::Typing) return;

    if (query_.empty()) {
        // 空のまま確定した: 絞り込みを解除する(検索のように、始める前の状態には戻らない)
        Clear();
        return;
    }
    mode_ = QueryMode::Committed;
    saved_.reset();
}

void FilterState::Cancel()
{
    if (mode_ != QueryMode::Typing) return;

    row_ranges_.clear();
    if (saved_) {
        query_ = std::move(saved_->query);
        kind_ = saved_->kind;
        mode_ = QueryMode::Committed;
    }
    else {
        query_.clear();
        kind_ = MatchKind::Substring;
        mode_ = QueryMode::Idle;
    }
    saved_.reset();
    anchor_ = origin_;
}

void FilterState::Set(std::string query, ListPosition anchor, MatchKind kind)
{
    if (query.empty()) {
        Clear();
        return;
    }
    mode_ = QueryMode::Committed;
    kind_ = kind;
    query_ = std::move(query);
    saved_.reset();
    row_ranges_.clear();
    origin_ = anchor;
    anchor_ = std::move(anchor);
}

void FilterState::Clear()
{
    mode_ = QueryMode::Idle;
    kind_ = MatchKind::Substring;
    query_.clear();
    saved_.reset();
    anchor_ = {};
    origin_ = {};
    row_ranges_.clear();
}

const std::vector<MatchRange>* FilterState::RangesFor(int row) const
{
    if (row < 0 || static_cast<size_t>(row) >= row_ranges_.size()) return nullptr;
    const auto& ranges = row_ranges_[static_cast<size_t>(row)];
    return ranges.empty() ? nullptr : &ranges;
}

} // namespace miata::views

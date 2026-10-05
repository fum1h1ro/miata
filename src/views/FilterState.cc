#include "FilterState.h"

#include <algorithm>
#include <format>

namespace miata::views {

std::string FilterCountText(const FilterStatus& status)
{
    if (status.mode == QueryMode::Idle || status.query.empty()) return "";

    std::string text = status.shown == 0 ? "見つかりません" : std::format("{}/{}", status.shown, status.total);
    if (status.hidden_marks > 0) text += std::format("  隠れたマーク {}", status.hidden_marks);
    return text;
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

void FilterState::Begin(ListPosition origin)
{
    if (mode_ == QueryMode::Typing) return;

    // 確定済みの絞り込みがあれば、Escで戻れるよう語を取っておく
    saved_query_ = mode_ == QueryMode::Committed ? std::optional<std::string>(query_) : std::nullopt;
    mode_ = QueryMode::Typing;
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
    saved_query_.reset();
}

void FilterState::Cancel()
{
    if (mode_ != QueryMode::Typing) return;

    row_ranges_.clear();
    if (saved_query_) {
        query_ = std::move(*saved_query_);
        mode_ = QueryMode::Committed;
    }
    else {
        query_.clear();
        mode_ = QueryMode::Idle;
    }
    saved_query_.reset();
    anchor_ = origin_;
}

void FilterState::Set(std::string query, ListPosition anchor)
{
    if (query.empty()) {
        Clear();
        return;
    }
    mode_ = QueryMode::Committed;
    query_ = std::move(query);
    saved_query_.reset();
    row_ranges_.clear();
    origin_ = anchor;
    anchor_ = std::move(anchor);
}

void FilterState::Clear()
{
    mode_ = QueryMode::Idle;
    query_.clear();
    saved_query_.reset();
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

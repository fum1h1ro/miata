#include "SearchState.h"

#include <algorithm>
#include <format>
#include <iterator>
#include <utility>

namespace miata::views {

void SearchState::Begin(ListPosition origin)
{
    if (mode_ == QueryMode::Typing) return;

    // 確定済みの検索があれば、Escで戻れるよう語を取っておく
    saved_query_ = mode_ == QueryMode::Committed ? std::optional<std::string>(query_) : std::nullopt;
    mode_ = QueryMode::Typing;
    query_.clear();
    hits_.clear();
    origin_ = origin;
    anchor_ = std::move(origin);
}

void SearchState::SetQuery(std::string query)
{
    if (mode_ != QueryMode::Typing) return;
    query_ = std::move(query);
}

void SearchState::Commit()
{
    if (mode_ != QueryMode::Typing) return;

    if (query_.empty()) {
        // 何も入力しなかった(または消した)。検索を始める前の状態に戻る
        Cancel();
        return;
    }
    mode_ = QueryMode::Committed;
    saved_query_.reset();
}

void SearchState::Cancel()
{
    if (mode_ != QueryMode::Typing) return;

    hits_.clear();
    if (saved_query_) {
        query_ = std::move(*saved_query_);
        mode_ = QueryMode::Committed;
    }
    else {
        query_.clear();
        mode_ = QueryMode::Idle;
    }
    saved_query_.reset();
}

void SearchState::Clear()
{
    mode_ = QueryMode::Idle;
    query_.clear();
    saved_query_.reset();
    anchor_ = {};
    origin_ = {};
    hits_.clear();
}

std::vector<SearchHit>::const_iterator SearchState::LowerBound(int row) const
{
    return std::lower_bound(hits_.begin(), hits_.end(), row, [](const SearchHit& hit, int r) {
        return hit.row < r;
    });
}

const std::vector<MatchRange>* SearchState::RangesFor(int row) const
{
    auto it = LowerBound(row);
    if (it == hits_.end() || it->row != row) return nullptr;
    return &it->ranges;
}

std::optional<int> SearchState::FirstHitFrom(int row) const
{
    if (hits_.empty()) return std::nullopt;
    auto it = LowerBound(row);
    return (it == hits_.end() ? hits_.front() : *it).row;
}

std::optional<int> SearchState::Step(int row, int dir) const
{
    if (hits_.empty()) return std::nullopt;

    auto it = LowerBound(row); // row以降の最初のマッチ
    if (dir >= 0) {
        if (it != hits_.end() && it->row == row) ++it; // rowそのものは含まない
        return (it == hits_.end() ? hits_.front() : *it).row;
    }
    return (it == hits_.begin() ? hits_.back() : *std::prev(it)).row;
}

int SearchState::Ordinal(int row) const
{
    auto it = LowerBound(row);
    if (it == hits_.end() || it->row != row) return 0;
    return static_cast<int>(std::distance(hits_.begin(), it)) + 1;
}

std::string SearchCountText(const SearchStatus& status)
{
    if (status.mode == QueryMode::Idle || status.query.empty()) return "";
    if (status.hit_count == 0) return "見つかりません";
    return std::format("{}/{}", status.ordinal > 0 ? std::to_string(status.ordinal) : "-", status.hit_count);
}

} // namespace miata::views

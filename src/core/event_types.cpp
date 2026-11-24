// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <core/event_types.hpp>

#include <parser/template_mask.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace nesso::core
{

EventTypeTally::EventTypeTally(std::size_t max_types, KeyFunction key)
    : key_(key ? std::move(key) : KeyFunction{&nesso::parser::MaskTemplate}), max_types_(max_types)
{
}

auto EventTypeTally::Add(const EventLine &line) -> bool
{
    std::string key = key_(line.text);
    const std::uint64_t ordinal = lines_ + 1;
    if (const auto found = ids_.find(key); found != ids_.end())
    {
        EventTypeStats &stats = types_[found->second];
        ++stats.count;
        stats.last_seen = ordinal;
        lines_ = ordinal;
        return true;
    }
    if (types_.size() >= max_types_)
    {
        return false;
    }
    ids_.emplace(key, types_.size());
    types_.push_back(EventTypeStats{.key = std::move(key),
                                    .count = 1,
                                    .first_seen = ordinal,
                                    .last_seen = ordinal,
                                    .source = std::string{line.source},
                                    .line = line.line,
                                    .array_element = line.array_element,
                                    .text = std::string{line.text}});
    lines_ = ordinal;
    return true;
}

auto EventTypeTally::Types() const noexcept -> std::span<const EventTypeStats> { return types_; }

auto EventTypeTally::Lines() const noexcept -> std::uint64_t { return lines_; }

static auto RareBefore(const EventTypeStats &left, const EventTypeStats &right) noexcept -> bool
{
    if (left.count != right.count)
    {
        return left.count < right.count;
    }
    return left.first_seen > right.first_seen;
}

static auto NewBefore(const EventTypeStats &left, const EventTypeStats &right) noexcept -> bool
{
    if (left.first_seen != right.first_seen)
    {
        return left.first_seen > right.first_seen;
    }
    return left.count < right.count;
}

auto RankEventTypes(std::span<const EventTypeStats> stats, EventOrder order, std::size_t top_k)
    -> std::vector<std::size_t>
{
    std::vector<std::size_t> indexes(stats.size());
    std::ranges::iota(indexes, std::size_t{0});
    const auto before = order == EventOrder::Rare ? &RareBefore : &NewBefore;
    const auto keep = static_cast<std::ptrdiff_t>(std::min(top_k, indexes.size()));
    std::ranges::partial_sort(indexes, indexes.begin() + keep,
                              [&stats, before](std::size_t left, std::size_t right) -> bool
                              { return before(stats[left], stats[right]); });
    indexes.resize(static_cast<std::size_t>(keep));
    return indexes;
}

} // namespace nesso::core

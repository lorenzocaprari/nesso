// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#ifndef CORE_EVENT_TYPES_HPP
#define CORE_EVENT_TYPES_HPP

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace nesso::core
{

/// One kept input line as `EventTypeTally::Add` sees it. The views only need
/// to live for the call.
struct EventLine
{
    std::string_view source;
    std::uint64_t line = 0;
    bool array_element = false;
    std::string_view text;
};

/// One event type: the key its lines share, how many kept lines have it, and
/// the first of those lines.
struct EventTypeStats
{
    std::string key;
    std::uint64_t count = 0;
    /// 1-based ordinals over the kept lines added to the tally.
    std::uint64_t first_seen = 0;
    std::uint64_t last_seen = 0;
    /// The first line with this key, as it was added.
    std::string source;
    std::uint64_t line = 0;
    bool array_element = false;
    std::string text;
};

/// Counts kept lines per event type, in one pass and in input order. The key
/// of a line is `parser::MaskTemplate` unless the constructor gets another
/// function.
class EventTypeTally
{
  public:
    using KeyFunction = std::function<std::string(std::string_view)>;

    /// At most `max_types` distinct keys. An empty `key` selects
    /// `parser::MaskTemplate`.
    explicit EventTypeTally(std::size_t max_types = std::numeric_limits<std::size_t>::max(), KeyFunction key = {});

    /// Records one kept line. Returns false, and records nothing, when the line
    /// has a new key and the tally already holds `max_types` keys.
    [[nodiscard]] auto Add(const EventLine &line) -> bool;

    /// Every type, in order of first appearance.
    [[nodiscard]] auto Types() const noexcept -> std::span<const EventTypeStats>;

    /// Kept lines recorded so far.
    [[nodiscard]] auto Lines() const noexcept -> std::uint64_t;

  private:
    KeyFunction key_;
    std::size_t max_types_;
    std::uint64_t lines_ = 0;
    std::vector<EventTypeStats> types_;
    std::unordered_map<std::string, std::size_t> ids_;
};

enum class EventOrder : std::uint8_t
{
    /// Fewest lines first. Equal counts: the type that appeared later first.
    Rare,
    /// The type that appeared later first. `first_seen` is unique per type.
    New
};

/// Indexes into `stats` of the first `top_k` types in `order`. Every key is an
/// integer, so the result does not depend on the sort algorithm.
[[nodiscard]] auto RankEventTypes(std::span<const EventTypeStats> stats, EventOrder order, std::size_t top_k)
    -> std::vector<std::size_t>;

} // namespace nesso::core

#endif // CORE_EVENT_TYPES_HPP

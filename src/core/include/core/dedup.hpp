// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#ifndef CORE_DEDUP_HPP
#define CORE_DEDUP_HPP

#include <cstddef>
#include <cstdint>
#include <deque>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace nesso::core
{

/// First-occurrence ids for masked chunk text. Pending texts are the raw lines not yet embedded.
class DistinctIndex
{
  public:
    /// Records one input line. Lines that share a masked key keep the id from the first line.
    [[nodiscard]] std::uint32_t add(std::string_view text);

    /// Reserves room for up to `lines` distinct texts.
    void reserve(std::size_t lines);

    /// Distinct texts added since the last `clearPending`, in first-occurrence order.
    [[nodiscard]] std::span<const std::string> pending() const noexcept;

    void clearPending() noexcept;

    /// The first raw line `add` stored for `id`. The view stays valid for the life of this index.
    [[nodiscard]] std::string_view text(std::uint32_t id) const noexcept;

    /// Input lines that share `id`'s masked key. At least 1 for an id `add` returned.
    [[nodiscard]] std::uint32_t occurrences(std::uint32_t id) const noexcept;

  private:
    struct TextHash
    {
        using is_transparent = void;

        [[nodiscard]] std::size_t operator()(std::string_view text) const noexcept
        {
            return std::hash<std::string_view>{}(text);
        }
    };

    struct TextEqual
    {
        using is_transparent = void;

        [[nodiscard]] bool operator()(std::string_view left, std::string_view right) const noexcept
        {
            return left == right;
        }
    };

    // deques so a string_view into a text or key stays valid when a later one is added.
    std::deque<std::string> texts_;
    std::deque<std::string> keys_;
    std::unordered_map<std::string_view, std::uint32_t, TextHash, TextEqual> ids_;
    std::vector<std::uint32_t> occurrences_;
    std::vector<std::string> pending_;
};

/// One count per input line: how many lines share that line's masked key. Each count is at least 1.
[[nodiscard]] std::vector<std::uint32_t> occurrenceCounts(std::span<const std::string_view> texts);

} // namespace nesso::core

#endif // CORE_DEDUP_HPP

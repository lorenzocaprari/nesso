// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#ifndef CORE_EMBED_ORDER_HPP
#define CORE_EMBED_ORDER_HPP

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace nesso::core
{

/// Distinct query terms `PromiseTerms` keeps, in first-occurrence order.
inline constexpr std::size_t PROMISE_TERM_LIMIT = 64;

/// How likely one embedding window is to hold a top match, from its lines'
/// raw text. Integers only, so the order does not depend on float math.
struct WindowPromise
{
    /// A line's raw text contains the stripped query.
    bool any_substr = false;
    /// The most query terms one line of the window contains.
    std::uint32_t max_hits = 0;
    /// Distinct ids of the window that occur on one input line only.
    std::uint32_t singletons = 0;
};

/// The first `PROMISE_TERM_LIMIT` distinct query terms, for counting how many
/// of them one line contains.
class PromiseTerms
{
  public:
    PromiseTerms() = default;
    explicit PromiseTerms(std::span<const std::string> query_terms);

    [[nodiscard]] auto Size() const noexcept -> std::size_t { return terms_.size(); }

    /// Kept query terms found in `line_terms`. Each counts once.
    [[nodiscard]] auto Hits(std::span<const std::string> line_terms) const -> std::uint32_t;

  private:
    // Sorted by term; the second member is the term's bit.
    std::vector<std::pair<std::string, std::uint64_t>> terms_;
};

/// Window indices, best first: `any_substr`, then `max_hits`, then
/// `singletons`, each descending, then ascending window index.
[[nodiscard]] auto EmbedOrder(std::span<const WindowPromise> windows) -> std::vector<std::size_t>;

} // namespace nesso::core

#endif // CORE_EMBED_ORDER_HPP

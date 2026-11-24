// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <core/embed_order.hpp>

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace nesso::core
{

PromiseTerms::PromiseTerms(std::span<const std::string> query_terms)
{
    for (const std::string &term : query_terms)
    {
        if (terms_.size() == PROMISE_TERM_LIMIT)
        {
            break;
        }
        const bool seen = std::ranges::any_of(terms_, [&term](const auto &kept) -> auto { return kept.first == term; });
        if (!seen)
        {
            terms_.emplace_back(term, std::uint64_t{1} << terms_.size());
        }
    }
    std::ranges::sort(terms_, {}, &std::pair<std::string, std::uint64_t>::first);
}

auto PromiseTerms::Hits(std::span<const std::string> line_terms) const -> std::uint32_t
{
    std::uint64_t found = 0;
    for (const std::string &term : line_terms)
    {
        const auto match = std::ranges::lower_bound(terms_, term, {}, &std::pair<std::string, std::uint64_t>::first);
        if (match != terms_.end() && match->first == term)
        {
            found |= match->second;
        }
    }
    return static_cast<std::uint32_t>(std::popcount(found));
}

auto EmbedOrder(std::span<const WindowPromise> windows) -> std::vector<std::size_t>
{
    std::vector<std::size_t> order(windows.size());
    std::ranges::iota(order, std::size_t{0});
    std::ranges::sort(order,
                      [windows](std::size_t left, std::size_t right) -> bool
                      {
                          const WindowPromise &first = windows[left];
                          const WindowPromise &second = windows[right];
                          if (first.any_substr != second.any_substr)
                          {
                              return first.any_substr;
                          }
                          if (first.max_hits != second.max_hits)
                          {
                              return first.max_hits > second.max_hits;
                          }
                          if (first.singletons != second.singletons)
                          {
                              return first.singletons > second.singletons;
                          }
                          return left < right;
                      });
    return order;
}

} // namespace nesso::core

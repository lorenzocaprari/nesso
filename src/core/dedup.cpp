// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <core/dedup.hpp>

#include <parser/template_mask.hpp>

namespace nesso::core
{

std::uint32_t DistinctIndex::add(std::string_view text)
{
    const std::string key = nesso::parser::maskTemplate(text);
    const auto found = ids_.find(key);
    if (found != ids_.end())
    {
        const std::uint32_t id = found->second;
        ++occurrences_[id];
        return id;
    }

    texts_.emplace_back(text);
    keys_.emplace_back(key);
    const auto id = static_cast<std::uint32_t>(occurrences_.size());
    ids_.emplace(keys_.back(), id);
    occurrences_.push_back(1);
    pending_.emplace_back(texts_.back());
    return id;
}

void DistinctIndex::reserve(std::size_t lines) { occurrences_.reserve(lines); }

std::span<const std::string> DistinctIndex::pending() const noexcept { return pending_; }

void DistinctIndex::clearPending() noexcept { pending_.clear(); }

std::string_view DistinctIndex::text(std::uint32_t id) const noexcept { return texts_[id]; }

std::uint32_t DistinctIndex::occurrences(std::uint32_t id) const noexcept { return occurrences_[id]; }

std::vector<std::uint32_t> occurrenceCounts(std::span<const std::string_view> texts)
{
    DistinctIndex index;
    index.reserve(texts.size());
    std::vector<std::uint32_t> ids;
    ids.reserve(texts.size());
    for (const std::string_view text : texts)
    {
        ids.push_back(index.add(text));
    }

    std::vector<std::uint32_t> counts(texts.size());
    for (std::size_t line = 0; line < texts.size(); ++line)
    {
        counts[line] = index.occurrences(ids[line]);
    }
    return counts;
}

} // namespace nesso::core

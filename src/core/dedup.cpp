// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <core/dedup.hpp>

#include <cstddef>
#include <cstdint>
#include <parser/template_mask.hpp>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace nesso::core
{

std::uint32_t DistinctIndex::Add(std::string_view text)
{
    const std::string key = nesso::parser::MaskTemplate(text);
    const auto found = ids_.find(key);
    if (found != ids_.end())
    {
        const std::uint32_t entry_id = found->second;
        ++occurrences_[entry_id];
        return entry_id;
    }

    texts_.emplace_back(text);
    keys_.emplace_back(key);
    const auto entry_id = static_cast<std::uint32_t>(occurrences_.size());
    ids_.emplace(keys_.back(), entry_id);
    occurrences_.push_back(1);
    pending_.emplace_back(texts_.back());
    return entry_id;
}

void DistinctIndex::Reserve(std::size_t lines) { occurrences_.reserve(lines); }

std::span<const std::string> DistinctIndex::Pending() const noexcept { return pending_; }

void DistinctIndex::ClearPending() noexcept { pending_.clear(); }

std::string_view DistinctIndex::Text(std::uint32_t entry_id) const noexcept { return texts_[entry_id]; }

std::uint32_t DistinctIndex::Occurrences(std::uint32_t entry_id) const noexcept { return occurrences_[entry_id]; }

std::vector<std::uint32_t> OccurrenceCounts(std::span<const std::string_view> texts)
{
    DistinctIndex index;
    index.Reserve(texts.size());
    std::vector<std::uint32_t> ids;
    ids.reserve(texts.size());
    for (const std::string_view text : texts)
    {
        ids.push_back(index.Add(text));
    }

    std::vector<std::uint32_t> counts(texts.size());
    for (std::size_t line = 0; line < texts.size(); ++line)
    {
        counts[line] = index.Occurrences(ids[line]);
    }
    return counts;
}

} // namespace nesso::core

// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <core/bm25.hpp>

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <utility>

namespace nesso::core
{

static bool termLess(std::string_view left, std::string_view right) noexcept
{
    const std::size_t count = left.size() < right.size() ? left.size() : right.size();
    for (std::size_t index = 0; index < count; ++index)
    {
        const auto leftByte = static_cast<unsigned char>(left[index]);
        const auto rightByte = static_cast<unsigned char>(right[index]);
        if (leftByte != rightByte)
        {
            return leftByte < rightByte;
        }
    }
    return left.size() < right.size();
}

static bool frequencyLess(const std::pair<std::string, std::size_t> &left,
                          const std::pair<std::string, std::size_t> &right) noexcept
{
    return termLess(left.first, right.first);
}

static std::size_t frequencyOf(const std::vector<std::pair<std::string, std::size_t>> &frequencies,
                               std::string_view term) noexcept
{
    for (const auto &entry : frequencies)
    {
        if (entry.first == term)
        {
            return entry.second;
        }
    }
    return 0;
}

static double idfOf(std::size_t lineCount, std::size_t documentFrequency) noexcept
{
    const double numerator = static_cast<double>(lineCount - documentFrequency) + 0.5;
    const double denominator = static_cast<double>(documentFrequency) + 0.5;
    return std::log(1.0 + (numerator / denominator));
}

static double termWeight(std::size_t frequency, std::size_t length, double averageLength) noexcept
{
    const auto tf = static_cast<double>(frequency);
    const double norm = (1.0 - BM25_B) + (BM25_B * (static_cast<double>(length) / averageLength));
    const double denominator = tf + (BM25_K1 * norm);
    return (tf * (BM25_K1 + 1.0)) / denominator;
}

void Bm25Index::addLine(std::span<const std::string_view> terms)
{
    std::unordered_map<std::string, std::size_t> counts;
    counts.reserve(terms.size());
    for (const std::string_view term : terms)
    {
        ++counts[std::string(term)];
    }

    Line line;
    line.length = terms.size();
    line.frequencies.reserve(counts.size());
    for (const auto &entry : counts)
    {
        ++documentFrequency_[entry.first];
        line.frequencies.emplace_back(entry.first, entry.second);
    }
    std::ranges::sort(line.frequencies, frequencyLess);
    totalLength_ += line.length;
    lines_.push_back(std::move(line));
}

std::size_t Bm25Index::lineCount() const noexcept { return lines_.size(); }

double Bm25Index::averageLength() const noexcept
{
    if (lines_.empty())
    {
        return 0.0;
    }
    return static_cast<double>(totalLength_) / static_cast<double>(lines_.size());
}

std::vector<double> Bm25Index::scores(std::span<const std::string_view> query) const
{
    std::vector<double> result(lines_.size(), 0.0);
    // avgdl is 0 only when every line is empty. Every frequency is then 0, and
    // the length norm would divide by avgdl.
    if (lines_.empty() || query.empty() || totalLength_ == 0)
    {
        return result;
    }

    const double averageLength = static_cast<double>(totalLength_) / static_cast<double>(lines_.size());
    std::vector<std::string> distinct;
    distinct.reserve(query.size());
    for (const std::string_view term : query)
    {
        distinct.emplace_back(term);
    }
    std::ranges::sort(distinct, termLess);
    std::vector<std::string> uniqueTerms;
    uniqueTerms.reserve(distinct.size());
    for (std::string &term : distinct)
    {
        if (uniqueTerms.empty() || uniqueTerms.back() != term)
        {
            uniqueTerms.push_back(std::move(term));
        }
    }
    distinct = std::move(uniqueTerms);

    for (const std::string &term : distinct)
    {
        const auto found = documentFrequency_.find(term);
        if (found == documentFrequency_.end())
        {
            continue;
        }
        const double idf = idfOf(lines_.size(), found->second);
        for (std::size_t line = 0; line < lines_.size(); ++line)
        {
            const std::size_t frequency = frequencyOf(lines_[line].frequencies, term);
            if (frequency == 0)
            {
                continue;
            }
            result[line] += idf * termWeight(frequency, lines_[line].length, averageLength);
        }
    }
    return result;
}

} // namespace nesso::core

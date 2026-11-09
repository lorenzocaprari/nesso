// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <core/bm25.hpp>

#include <algorithm>
#include <cmath>
#include <optional>
#include <span>
#include <string>
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

static std::uint32_t frequencyOf(std::span<const std::pair<std::uint32_t, std::uint32_t>> frequencies,
                                 std::uint32_t termId) noexcept
{
    for (const auto &entry : frequencies)
    {
        if (entry.first == termId)
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

static double termWeight(std::uint32_t frequency, std::uint32_t length, double averageLength) noexcept
{
    const auto tf = static_cast<double>(frequency);
    const double norm = (1.0 - BM25_B) + (BM25_B * (static_cast<double>(length) / averageLength));
    const double denominator = tf + (BM25_K1 * norm);
    return (tf * (BM25_K1 + 1.0)) / denominator;
}

static std::vector<std::string> distinctQueryTerms(std::span<const std::string_view> query)
{
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
    return uniqueTerms;
}

static std::optional<std::uint32_t> dictionaryId(std::span<const std::string_view> dictionary,
                                                 std::string_view term) noexcept
{
    const auto found = std::ranges::lower_bound(dictionary, term, termLess);
    if (found == dictionary.end() || *found != term)
    {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(found - dictionary.begin());
}

static std::vector<double> scoreLines(std::size_t lineCount, std::size_t totalLength,
                                      std::span<const std::size_t> documentFrequency, auto &&lineAt,
                                      std::span<const std::string_view> query, auto &&resolveId)
{
    std::vector<double> result(lineCount, 0.0);
    if (lineCount == 0 || query.empty() || totalLength == 0)
    {
        return result;
    }

    const double averageLength = static_cast<double>(totalLength) / static_cast<double>(lineCount);
    const std::vector<std::string> distinct = distinctQueryTerms(query);

    for (const std::string &term : distinct)
    {
        const std::optional<std::uint32_t> termId = resolveId(term);
        if (!termId)
        {
            continue;
        }
        const std::size_t df = documentFrequency[*termId];
        if (df == 0)
        {
            continue;
        }
        const double idf = idfOf(lineCount, df);
        for (std::size_t line = 0; line < lineCount; ++line)
        {
            const auto [frequencies, length] = lineAt(line);
            const std::uint32_t frequency = frequencyOf(frequencies, *termId);
            if (frequency == 0)
            {
                continue;
            }
            result[line] += idf * termWeight(frequency, length, averageLength);
        }
    }
    return result;
}

std::uint32_t Bm25Index::internTerm(std::string_view term)
{
    std::string key{term};
    if (const auto found = ids_.find(key); found != ids_.end())
    {
        return found->second;
    }
    const auto id = static_cast<std::uint32_t>(documentFrequency_.size());
    ids_.emplace(std::move(key), id);
    documentFrequency_.push_back(0);
    return id;
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
    line.length = static_cast<std::uint32_t>(terms.size());
    line.frequencies.reserve(counts.size());
    for (const auto &entry : counts)
    {
        const std::uint32_t id = internTerm(entry.first);
        ++documentFrequency_[id];
        line.frequencies.emplace_back(id, static_cast<std::uint32_t>(entry.second));
    }
    std::ranges::sort(line.frequencies, [](const auto &left, const auto &right) { return left.first < right.first; });
    totalLength_ += line.length;
    lines_.push_back(std::move(line));
}

void Bm25Index::addLinePostings(std::span<const std::pair<std::uint32_t, std::uint32_t>> entries, std::uint32_t length)
{
    Line line;
    line.length = length;
    line.frequencies.assign(entries.begin(), entries.end());
    std::ranges::sort(line.frequencies, [](const auto &left, const auto &right) { return left.first < right.first; });
    for (const auto &[termId, frequency] : line.frequencies)
    {
        (void)frequency;
        if (termId >= documentFrequency_.size())
        {
            continue;
        }
        ++documentFrequency_[termId];
    }
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
    return scoreLines(
        lines_.size(), totalLength_, documentFrequency_,
        [this](std::size_t line)
        {
            return std::pair{std::span<const std::pair<std::uint32_t, std::uint32_t>>{lines_[line].frequencies},
                             lines_[line].length};
        },
        query,
        [this](std::string_view term) -> std::optional<std::uint32_t>
        {
            const auto found = ids_.find(std::string{term});
            if (found == ids_.end())
            {
                return std::nullopt;
            }
            return found->second;
        });
}

std::vector<double> scoresFromPostings(std::span<const std::string_view> dictionary,
                                       std::span<const Bm25LinePostings> lines, std::span<const std::string_view> query)
{
    std::vector<std::size_t> documentFrequency(dictionary.size(), 0);
    std::size_t totalLength = 0;
    for (const Bm25LinePostings &line : lines)
    {
        totalLength += line.length;
        for (const auto &[termId, frequency] : line.entries)
        {
            (void)frequency;
            if (termId < documentFrequency.size())
            {
                ++documentFrequency[termId];
            }
        }
    }
    return scoreLines(
        lines.size(), totalLength, documentFrequency,
        [&lines](std::size_t line) { return std::pair{lines[line].entries, lines[line].length}; }, query,
        [&dictionary](std::string_view term) { return dictionaryId(dictionary, term); });
}

} // namespace nesso::core

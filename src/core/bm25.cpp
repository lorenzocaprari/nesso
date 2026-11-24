// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <core/bm25.hpp>
#include <core/lexical_terms.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace nesso::core
{

static std::uint32_t FrequencyOf(std::span<const std::pair<std::uint32_t, std::uint32_t>> frequencies,
                                 std::uint32_t term_id) noexcept
{
    for (const auto &entry : frequencies)
    {
        if (entry.first == term_id)
        {
            return entry.second;
        }
    }
    return 0;
}

static double IdfOf(std::size_t line_count, std::size_t document_frequency) noexcept
{
    const double numerator = static_cast<double>(line_count - document_frequency) + 0.5;
    const double denominator = static_cast<double>(document_frequency) + 0.5;
    return std::log(1.0 + (numerator / denominator));
}

namespace
{
struct TermWeightInput
{
    std::uint32_t frequency = 0;
    std::uint32_t length = 0;
    double average_length = 0.0;
};
} // namespace

static double TermWeight(const TermWeightInput &input) noexcept
{
    const auto term_freq = static_cast<double>(input.frequency);
    const double norm = (1.0 - BM25_B) + (BM25_B * (static_cast<double>(input.length) / input.average_length));
    const double denominator = term_freq + (BM25_K1 * norm);
    return (term_freq * (BM25_K1 + 1.0)) / denominator;
}

static std::vector<std::string> DistinctQueryTerms(std::span<const std::string_view> query)
{
    std::vector<std::string> distinct;
    distinct.reserve(query.size());
    for (const std::string_view term : query)
    {
        distinct.emplace_back(term);
    }
    std::ranges::sort(distinct, TermByteLess);
    std::vector<std::string> unique_terms;
    unique_terms.reserve(distinct.size());
    for (std::string &term : distinct)
    {
        if (unique_terms.empty() || unique_terms.back() != term)
        {
            unique_terms.push_back(std::move(term));
        }
    }
    return unique_terms;
}

static std::optional<std::uint32_t> DictionaryId(std::span<const std::string_view> dictionary,
                                                 std::string_view term) noexcept
{
    const auto found = std::ranges::lower_bound(dictionary, term, TermByteLess);
    if (found == dictionary.end() || *found != term)
    {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(found - dictionary.begin());
}

static Bm25Result ScoreLines(std::size_t line_count, std::size_t total_length,
                             std::span<const std::size_t> document_frequency, auto &&line_at,
                             std::span<const std::string_view> query, auto &&resolve_id)
{
    Bm25Result result{.scores = std::vector<double>(line_count, 0.0), .coverage = std::vector<double>(line_count, 0.0)};
    if (line_count == 0 || query.empty() || total_length == 0)
    {
        return result;
    }

    const double average_length = static_cast<double>(total_length) / static_cast<double>(line_count);
    const std::vector<std::string> distinct = DistinctQueryTerms(query);
    // Integer counts, divided once at the end, so coverage does not depend on
    // the order terms were visited.
    std::vector<std::uint32_t> matched(line_count, 0);

    for (const std::string &term : distinct)
    {
        const std::optional<std::uint32_t> term_id = resolve_id(term);
        if (!term_id)
        {
            continue;
        }
        const std::size_t doc_freq = document_frequency[*term_id];
        if (doc_freq == 0)
        {
            continue;
        }
        const double idf = IdfOf(line_count, doc_freq);
        for (std::size_t line = 0; line < line_count; ++line)
        {
            const auto [frequencies, length] = line_at(line);
            const std::uint32_t frequency = FrequencyOf(frequencies, *term_id);
            if (frequency == 0)
            {
                continue;
            }
            result.scores[line] +=
                idf * TermWeight({.frequency = frequency, .length = length, .average_length = average_length});
            ++matched[line];
        }
    }
    if (!distinct.empty())
    {
        const auto terms = static_cast<double>(distinct.size());
        for (std::size_t line = 0; line < line_count; ++line)
        {
            result.coverage[line] = static_cast<double>(matched[line]) / terms;
        }
    }
    return result;
}

std::uint32_t Bm25Index::InternTerm(std::string_view term)
{
    std::string key{term};
    if (const auto found = ids_.find(key); found != ids_.end())
    {
        return found->second;
    }
    const auto term_id = static_cast<std::uint32_t>(document_frequency_.size());
    ids_.emplace(std::move(key), term_id);
    document_frequency_.push_back(0);
    return term_id;
}

void Bm25Index::AddLine(std::span<const std::string_view> terms)
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
        const std::uint32_t term_id = InternTerm(entry.first);
        ++document_frequency_[term_id];
        line.frequencies.emplace_back(term_id, static_cast<std::uint32_t>(entry.second));
    }
    std::ranges::sort(line.frequencies, [](const auto &left, const auto &right) { return left.first < right.first; });
    total_length_ += line.length;
    lines_.push_back(std::move(line));
}

void Bm25Index::AddLinePostings(std::span<const std::pair<std::uint32_t, std::uint32_t>> entries, std::uint32_t length)
{
    Line line;
    line.length = length;
    line.frequencies.assign(entries.begin(), entries.end());
    std::ranges::sort(line.frequencies, [](const auto &left, const auto &right) { return left.first < right.first; });
    for (const auto &[term_id, frequency] : line.frequencies)
    {
        (void)frequency;
        if (term_id >= document_frequency_.size())
        {
            continue;
        }
        ++document_frequency_[term_id];
    }
    total_length_ += line.length;
    lines_.push_back(std::move(line));
}

std::size_t Bm25Index::LineCount() const noexcept { return lines_.size(); }

double Bm25Index::AverageLength() const noexcept
{
    if (lines_.empty())
    {
        return 0.0;
    }
    return static_cast<double>(total_length_) / static_cast<double>(lines_.size());
}

std::vector<double> Bm25Index::Scores(std::span<const std::string_view> query) const
{
    return ScoresAndCoverage(query).scores;
}

Bm25Result Bm25Index::ScoresAndCoverage(std::span<const std::string_view> query) const
{
    return ScoreLines(
        lines_.size(), total_length_, document_frequency_,
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

std::vector<double> ScoresFromPostings(std::span<const std::string_view> dictionary,
                                       std::span<const Bm25LinePostings> lines, std::span<const std::string_view> query)
{
    return ScoresAndCoverageFromPostings(dictionary, lines, query).scores;
}

Bm25Result ScoresAndCoverageFromPostings(std::span<const std::string_view> dictionary,
                                         std::span<const Bm25LinePostings> lines,
                                         std::span<const std::string_view> query)
{
    std::vector<std::size_t> document_frequency(dictionary.size(), 0);
    std::size_t total_length = 0;
    for (const Bm25LinePostings &line : lines)
    {
        total_length += line.length;
        for (const auto &[term_id, frequency] : line.entries)
        {
            (void)frequency;
            if (term_id < document_frequency.size())
            {
                ++document_frequency[term_id];
            }
        }
    }
    return ScoreLines(
        lines.size(), total_length, document_frequency,
        [&lines](std::size_t line) { return std::pair{lines[line].entries, lines[line].length}; }, query,
        [&dictionary](std::string_view term) { return DictionaryId(dictionary, term); });
}

} // namespace nesso::core

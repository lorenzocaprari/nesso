// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT
#include "include/mach_core/bm25_index.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <unordered_map>

namespace mach_core
{
namespace
{

constexpr float kBm25K1 = 1.5f;
constexpr float kBm25B = 0.75f;

[[nodiscard]] char asciiLower(char c) noexcept
{
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

[[nodiscard]] bool isHex(char c) noexcept
{
    const auto u = static_cast<unsigned char>(c);
    return std::isxdigit(u) != 0;
}

} // namespace

std::vector<std::string> lexicalTokens(std::string_view text)
{
    std::vector<std::string> tokens;
    std::string current;
    const auto flush = [&]()
    {
        if (!current.empty())
        {
            tokens.push_back(current);
            current.clear();
        }
    };

    for (size_t i = 0; i < text.size(); ++i)
    {
        const char c = asciiLower(text[i]);
        if (c == '0' && i + 1 < text.size() && asciiLower(text[i + 1]) == 'x')
        {
            flush();
            std::string hex = "0x";
            i += 2;
            while (i < text.size() && isHex(text[i]))
            {
                hex.push_back(asciiLower(text[i]));
                ++i;
            }
            --i;
            tokens.push_back(std::move(hex));
            continue;
        }
        if (std::isalnum(static_cast<unsigned char>(c)) != 0)
        {
            current.push_back(c);
        }
        else
        {
            flush();
        }
    }
    flush();
    return tokens;
}

void Bm25Index::clear() noexcept
{
    m_postings.clear();
    m_docLength.clear();
    m_docCount = 0;
    m_avgDl = 0.0;
}

void Bm25Index::addDocument(uint64_t docId, std::string_view text)
{
    const auto tokens = lexicalTokens(text);
    m_docLength[docId] = static_cast<uint32_t>(tokens.size());
    double totalLen = m_avgDl * static_cast<double>(m_docCount);
    totalLen += static_cast<double>(tokens.size());
    m_docCount += 1;
    m_avgDl = m_docCount == 0 ? 0.0 : totalLen / static_cast<double>(m_docCount);

    std::unordered_map<std::string, uint32_t> seen;
    for (const auto &token : tokens)
    {
        if (seen[token]++ == 0)
        {
            m_postings[token].push_back(static_cast<uint32_t>(docId));
        }
    }
}

std::vector<SearchResult<float>> Bm25Index::search(std::string_view query, size_t k) const
{
    if (k == 0 || m_docCount == 0)
    {
        return {};
    }

    const auto queryTokens = lexicalTokens(query);
    std::unordered_map<uint64_t, float> scores;
    const auto n = static_cast<double>(m_docCount);

    for (const auto &token : queryTokens)
    {
        const auto it = m_postings.find(token);
        if (it == m_postings.end())
        {
            continue;
        }
        const auto df = static_cast<double>(it->second.size());
        const float idf = static_cast<float>(std::log((n - df + 0.5) / (df + 0.5) + 1.0));
        for (const uint32_t docId : it->second)
        {
            const auto lenIt = m_docLength.find(docId);
            const auto dl = lenIt == m_docLength.end() ? 0.0f : static_cast<float>(lenIt->second);
            const float tf = 1.0f;
            const float denom = tf + kBm25K1 * (1.0f - kBm25B + kBm25B * dl / static_cast<float>(m_avgDl == 0.0 ? 1.0 : m_avgDl));
            scores[docId] += idf * (tf * (kBm25K1 + 1.0f)) / denom;
        }
    }

    std::vector<SearchResult<float>> results;
    results.reserve(scores.size());
    for (const auto &[id, score] : scores)
    {
        results.push_back({.index = id, .score = score});
    }
    const auto order = [](const SearchResult<float> &a, const SearchResult<float> &b)
    { return a.score != b.score ? a.score > b.score : a.index < b.index; };
    const size_t resultCount = std::min(k, results.size());
    std::partial_sort(results.begin(), results.begin() + static_cast<std::ptrdiff_t>(resultCount), results.end(),
                      order);
    results.resize(resultCount);
    return results;
}

} // namespace mach_core

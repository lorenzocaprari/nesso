// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include "corpus_detail.hpp"

#include <core/bm25.hpp>
#include <core/corpus_index.hpp>
#include <core/lexical_terms.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace nesso::core::detail
{

static bool validKey(std::string_view key)
{
    return !key.empty() && std::ranges::all_of(key,
                                               [](char character)
                                               {
                                                   return (character >= 'a' && character <= 'z') ||
                                                          (character >= '0' && character <= '9') || character == '.' ||
                                                          character == '-' || character == '_';
                                               });
}

static bool validHash(std::string_view value)
{
    return value.size() == 64 &&
           std::ranges::all_of(
               value, [](char character)
               { return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f'); });
}

static bool parseNumber(std::string_view text, std::uint32_t &value)
{
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() && std::to_string(value) == text;
}
std::expected<std::uint32_t, EngineError> validatePolicy(const CorpusPolicy &policy)
{
    constexpr std::array<std::string_view, 10> required{
        "batch",        "dedup",      "dimensions", "mask",        "model.id", "model.quantization",
        "model.sha256", "tokens.max", "truncate",   "vocab.sha256"};
    for (const std::string_view key : required)
    {
        if (!policy.contains(key))
        {
            return std::unexpected(EngineError::CorruptDatabase);
        }
    }
    std::uint64_t size = 0;
    for (const auto &[key, value] : policy)
    {
        if (!validKey(key) || value.empty() || value.find_first_of("\r\n\0", 0, 3) != std::string::npos)
        {
            return std::unexpected(EngineError::CorruptDatabase);
        }
        size += key.size() + value.size() + 2;
        if (size > MAX_POLICY_BYTES)
        {
            return std::unexpected(EngineError::CorruptDatabase);
        }
    }
    std::uint32_t dimensions = 0;
    std::uint32_t maxTokens = 0;
    const auto &quantization = policy.at("model.quantization");
    if (!parseNumber(policy.at("dimensions"), dimensions) || dimensions > MAX_DIMENSIONS ||
        !parseNumber(policy.at("tokens.max"), maxTokens) || maxTokens == 0 || policy.at("model.id").size() > 1024 ||
        !validHash(policy.at("model.sha256")) || !validHash(policy.at("vocab.sha256")) ||
        (quantization != "int8" && quantization != "fp32"))
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    return dimensions;
}

LexicalStatus lexicalStatusOf(bool hasLexical, const CorpusPolicy &policy)
{
    if (!hasLexical)
    {
        return LexicalStatus::Missing;
    }
    const bool termsOk = policy.contains("lexical.terms") && policy.at("lexical.terms") == LEXICAL_TERMS_VERSION;
    const bool bm25Ok = policy.contains("bm25") && policy.at("bm25") == BM25_POLICY;
    if (!termsOk || !bm25Ok)
    {
        return LexicalStatus::VersionMismatch;
    }
    return LexicalStatus::Ok;
}

std::expected<void, EngineError> validateVector(std::span<const float> embedding, std::uint32_t dimensions)
{
    if (embedding.empty())
    {
        return std::unexpected(EngineError::DatabaseNotInitialized);
    }
    if (embedding.size() != dimensions)
    {
        return std::unexpected(EngineError::MismatchedDimensions);
    }
    if (!std::ranges::all_of(embedding, [](float value) { return std::isfinite(value); }))
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    return {};
}

std::string serializePolicy(const CorpusPolicy &policy)
{
    std::string text;
    for (const auto &[key, value] : policy)
    {
        text += key;
        text += '=';
        text += value;
        text += '\n';
    }
    return text;
}

std::expected<CorpusPolicy, EngineError> parsePolicy(std::string_view text)
{
    CorpusPolicy policy;
    std::string previous;
    while (!text.empty())
    {
        const auto newline = text.find('\n');
        if (newline == std::string_view::npos)
        {
            return std::unexpected(EngineError::CorruptDatabase);
        }
        const std::string_view record = text.substr(0, newline);
        const auto separator = record.find('=');
        if (separator == std::string_view::npos)
        {
            return std::unexpected(EngineError::CorruptDatabase);
        }
        std::string key{record.substr(0, separator)};
        if (!previous.empty() && key <= previous)
        {
            return std::unexpected(EngineError::CorruptDatabase);
        }
        previous = key;
        policy.emplace(std::move(key), std::string{record.substr(separator + 1)});
        text.remove_prefix(newline + 1);
    }
    if (!validatePolicy(policy))
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    return policy;
}

} // namespace nesso::core::detail

// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#ifndef NESSO_TYPES_HPP
#define NESSO_TYPES_HPP

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace nesso
{

inline constexpr size_t DEFAULT_TEXT_TOP_K = 5;
inline constexpr size_t DEFAULT_STORE_TOP_K = 10;
inline constexpr uint64_t DEFAULT_STORE_DIMENSIONS = 128;

struct GrepRequest
{
    std::string query;
    std::vector<std::filesystem::path> files;
    size_t topK = DEFAULT_TEXT_TOP_K;
};

struct IndexRequest
{
    std::vector<std::filesystem::path> files;
    std::filesystem::path output;
};

struct SearchRequest
{
    std::string query;
    std::filesystem::path index;
    size_t topK = DEFAULT_TEXT_TOP_K;
};

struct StoreInitRequest
{
    std::filesystem::path db;
    uint64_t dimensions = DEFAULT_STORE_DIMENSIONS;
};

struct StoreIngestRequest
{
    std::filesystem::path db;
    uint64_t dimensions = DEFAULT_STORE_DIMENSIONS;
    std::filesystem::path input;
};

struct StoreSearchRequest
{
    std::filesystem::path db;
    uint64_t dimensions = DEFAULT_STORE_DIMENSIONS;
    std::filesystem::path query;
    size_t topK = DEFAULT_STORE_TOP_K;
};

struct TextMatch
{
    std::string source;
    uint64_t line = 0;
    float score = 0.0F;
    std::string text;
};

struct TextResults
{
    std::vector<TextMatch> matches;
    size_t skippedLines = 0;
    bool multipleSources = false;
};

struct IndexSummary
{
    size_t chunks = 0;
    size_t skippedLines = 0;
};

struct StoreInitSummary
{
    uint64_t dimensions = 0;
};

struct StoreIngestSummary
{
    uint64_t before = 0;
    size_t added = 0;
    uint64_t total = 0;
    std::optional<int> appendFailureCode;
};

struct VectorMatch
{
    uint64_t index = 0;
    float score = 0.0F;
};

} // namespace nesso

#endif // NESSO_TYPES_HPP

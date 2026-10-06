// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#ifndef CORE_CORPUS_INDEX_HPP
#define CORE_CORPUS_INDEX_HPP

#include "core_types.hpp"

#include <array>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace core
{

inline constexpr std::array<uint8_t, 4> CORPUS_MAGIC = {'N', 'E', 'S', 'C'};
inline constexpr uint32_t CORPUS_VERSION = 1;

struct LogChunk
{
    std::string text;
    uint64_t lineNumber = 0;
    std::string source;
};

struct CorpusChunk
{
    LogChunk chunk{};
    std::vector<float> embedding;
};

[[nodiscard]] std::expected<void, EngineError> writeCorpusFile(const std::filesystem::path &path,
                                                               std::span<const CorpusChunk> chunks);

[[nodiscard]] std::expected<std::vector<CorpusChunk>, EngineError> readCorpusFile(const std::filesystem::path &path);

} // namespace core

#endif // CORE_CORPUS_INDEX_HPP

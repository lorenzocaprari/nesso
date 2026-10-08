// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#ifndef CORE_CORPUS_INDEX_HPP
#define CORE_CORPUS_INDEX_HPP

#include "core_types.hpp"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace core
{

inline constexpr std::uint32_t CORPUS_VERSION = 2;

struct LogChunk
{
    std::string text;
    std::uint64_t lineNumber = 0;
    std::string source;
    std::uint64_t byteOffset = 0;
};

struct CorpusChunk
{
    LogChunk chunk{};
    std::vector<float> embedding;
};

/// A corpus mapped so the float block is the top-k matrix. Chunk text is owned;
/// `matrix` views the mapping.
class MappedCorpus
{
  public:
    ~MappedCorpus();
    MappedCorpus(const MappedCorpus &) = delete;
    MappedCorpus &operator=(const MappedCorpus &) = delete;
    MappedCorpus(MappedCorpus &&) noexcept;
    MappedCorpus &operator=(MappedCorpus &&) noexcept;

    [[nodiscard]] std::string_view modelId() const noexcept;
    [[nodiscard]] std::uint32_t dimensions() const noexcept;
    [[nodiscard]] std::span<const CorpusChunk> chunks() const noexcept;
    [[nodiscard]] std::span<const float> matrix() const noexcept;

  private:
    struct Impl;
    friend std::expected<MappedCorpus, EngineError> mapCorpusFile(const std::filesystem::path &path);
    explicit MappedCorpus(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

[[nodiscard]] std::expected<void, EngineError>
writeCorpusFile(const std::filesystem::path &path, std::span<const CorpusChunk> chunks, std::string_view modelId);

[[nodiscard]] std::expected<MappedCorpus, EngineError> mapCorpusFile(const std::filesystem::path &path);

[[nodiscard]] std::expected<std::vector<CorpusChunk>, EngineError> readCorpusFile(const std::filesystem::path &path);

} // namespace core

#endif // CORE_CORPUS_INDEX_HPP

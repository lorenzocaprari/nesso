// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#ifndef CORE_CORPUS_INDEX_HPP
#define CORE_CORPUS_INDEX_HPP

#include <core/core_types.hpp>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace nesso::core
{

inline constexpr std::uint32_t CORPUS_VERSION = 2;

struct LogChunk
{
    std::string text;
    std::uint64_t lineNumber = 0;
    std::string source;
    std::uint64_t byteOffset = 0;
    bool arrayElement = false;
};

struct CorpusChunk
{
    LogChunk chunk{};
    std::vector<float> embedding;
};

struct SourceInfo
{
    std::string path;
    std::uint64_t size = 0;
    std::int64_t modifiedTime = 0;
    bool tracked = false;
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
    [[nodiscard]] std::span<const SourceInfo> sources() const noexcept;
    [[nodiscard]] std::span<const float> matrix() const noexcept;

  private:
    struct Impl;
    friend std::expected<MappedCorpus, EngineError> mapCorpusFile(const std::filesystem::path &path);
    explicit MappedCorpus(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

[[nodiscard]] std::expected<void, EngineError>
writeCorpusFile(const std::filesystem::path &path, std::span<const CorpusChunk> chunks, std::string_view modelId);

/// Writes a corpus from batches. Embeddings are spilled to a side file so the caller can drop each batch.
class CorpusWriter
{
  public:
    ~CorpusWriter();
    CorpusWriter(const CorpusWriter &) = delete;
    CorpusWriter &operator=(const CorpusWriter &) = delete;
    CorpusWriter(CorpusWriter &&other) noexcept;
    CorpusWriter &operator=(CorpusWriter &&other) noexcept;

    [[nodiscard]] static std::expected<CorpusWriter, EngineError> create(const std::filesystem::path &path,
                                                                         std::string_view modelId);
    [[nodiscard]] std::expected<void, EngineError> add(std::span<const CorpusChunk> chunks);
    [[nodiscard]] std::expected<void, EngineError> finish();

  private:
    CorpusWriter() = default;

    std::filesystem::path destination_;
    std::filesystem::path recordsPath_;
    std::ofstream records_;
    std::string modelId_;
    std::uint32_t dimensions_ = 0;
    std::uint64_t chunkCount_ = 0;
    bool open_ = false;
};

[[nodiscard]] std::expected<MappedCorpus, EngineError> mapCorpusFile(const std::filesystem::path &path);

[[nodiscard]] std::expected<std::vector<CorpusChunk>, EngineError> readCorpusFile(const std::filesystem::path &path);

} // namespace nesso::core

#endif // CORE_CORPUS_INDEX_HPP

// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#ifndef CORE_CORPUS_INDEX_HPP
#define CORE_CORPUS_INDEX_HPP

#include <core/core_types.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace nesso::core
{

inline constexpr std::uint32_t CORPUS_VERSION = 3;
inline constexpr std::uint32_t NO_DISTINCT = std::numeric_limits<std::uint32_t>::max();
inline constexpr std::size_t CORPUS_CHECKSUM_OFFSET = 112;
inline constexpr std::size_t CORPUS_CHECKSUM_BYTES = 16;

using CorpusChecksum = std::array<std::byte, CORPUS_CHECKSUM_BYTES>;

using CorpusPolicy = std::map<std::string, std::string, std::less<>>;

struct LogChunk
{
    std::string text;
    std::uint64_t lineNumber = 0;
    std::string source;
    std::uint64_t byteOffset = 0;
    bool arrayElement = false;
    std::uint64_t byteLength = 0;
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
    std::string jsonField = "message";
    bool storedOnly = false;
};

struct CorpusLine
{
    std::uint64_t lineNumber = 0;
    std::uint64_t byteOffset = 0;
    std::uint64_t byteLength = 0;
    std::uint32_t sourceIndex = 0;
    std::uint32_t distinctId = NO_DISTINCT;
    bool arrayElement = false;
    std::optional<std::string_view> storedText = std::nullopt;
};

/// One corpus line's BM25 postings: `|D|` and unique `(termId, tf)` pairs.
struct LexicalLinePostings
{
    std::uint32_t length = 0;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> entries;
};

/// Lexical readiness of a mapped corpus for hybrid/lexical/`auto` search.
/// Corrupt postings still fail at map time; only missing or stale policy is soft.
enum class LexicalStatus : std::uint8_t
{
    Ok,
    Missing,
    VersionMismatch
};

/// Capture metadata used to detect changes before re-reading a source.
[[nodiscard]] std::expected<SourceInfo, EngineError> sourceInfoFor(const std::filesystem::path &path,
                                                                   std::string_view jsonField);

/// Text and matrix views remain valid while the mapped corpus is alive.
class MappedCorpus
{
  public:
    ~MappedCorpus();
    MappedCorpus(const MappedCorpus &) = delete;
    MappedCorpus &operator=(const MappedCorpus &) = delete;
    MappedCorpus(MappedCorpus &&) noexcept;
    MappedCorpus &operator=(MappedCorpus &&) noexcept;

    [[nodiscard]] const CorpusPolicy &policy() const noexcept;
    [[nodiscard]] std::uint32_t dimensions() const noexcept;
    [[nodiscard]] std::span<const SourceInfo> sources() const noexcept;
    [[nodiscard]] std::span<const CorpusLine> lines() const noexcept;
    [[nodiscard]] std::span<const std::string_view> texts() const noexcept;
    [[nodiscard]] std::span<const float> matrix() const noexcept;
    /// Sorted unique lexical terms; ids in `lexicalPostings()` index this table.
    [[nodiscard]] std::span<const std::string_view> lexicalDictionary() const noexcept;
    /// One posting list per corpus line, including skipped lines.
    [[nodiscard]] std::span<const LexicalLinePostings> lexicalPostings() const noexcept;
    /// Soft lexical gate for non-dense modes. Dense search ignores this.
    // NOLINTNEXTLINE(readability-identifier-naming)
    [[nodiscard]] LexicalStatus GetLexicalStatus() const noexcept;
    /// Corpus `lexical.terms` policy value when present; empty when absent.
    [[nodiscard]] std::string_view corpusLexicalTermsVersion() const noexcept;

  private:
    struct Impl;
    friend std::expected<MappedCorpus, EngineError> mapCorpusFile(const std::filesystem::path &path,
                                                                  std::uint32_t *observedVersion);
    explicit MappedCorpus(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

/// Spills ordered line records and distinct texts/vectors before atomically replacing the destination.
class CorpusWriter
{
  public:
    ~CorpusWriter();
    CorpusWriter(const CorpusWriter &) = delete;
    CorpusWriter &operator=(const CorpusWriter &) = delete;
    CorpusWriter(CorpusWriter &&) noexcept;
    CorpusWriter &operator=(CorpusWriter &&other) noexcept;

    [[nodiscard]] static std::expected<CorpusWriter, EngineError> create(const std::filesystem::path &path,
                                                                         CorpusPolicy policy);
    [[nodiscard]] std::expected<std::uint32_t, EngineError> addSource(SourceInfo source);
    /// Reserve a distinct id before its embedding is available, so lines can be streamed immediately.
    [[nodiscard]] std::expected<std::uint32_t, EngineError> reserveDistinct(std::string_view text);
    /// Write vectors in distinct-id order. Every reserved id must have a vector before finish.
    [[nodiscard]] std::expected<void, EngineError> addVector(std::uint32_t id, std::span<const float> embedding);
    [[nodiscard]] std::expected<std::uint32_t, EngineError> addDistinct(std::string_view text,
                                                                        std::span<const float> embedding);
    /// `lineTerms.size()` must equal `lines.size()`. Each entry is the term
    /// multiset for that line (`lexicalTerms`); skipped lines use an empty list.
    [[nodiscard]] std::expected<void, EngineError> addLines(std::span<const CorpusLine> lines,
                                                            std::span<const std::vector<std::string>> lineTerms);
    [[nodiscard]] std::expected<void, EngineError> finish();

  private:
    struct Impl;
    explicit CorpusWriter(std::unique_ptr<Impl> impl);
    static void discardSpills(Impl *impl) noexcept;
    std::unique_ptr<Impl> impl_;
};

/// When supplied, `observedVersion` receives the version even when it is unsupported.
[[nodiscard]] std::expected<MappedCorpus, EngineError> mapCorpusFile(const std::filesystem::path &path,
                                                                     std::uint32_t *observedVersion = nullptr);

/// Expands distinct vectors for callers that need owned per-line chunks.
/// First 16 bytes of the SHA-256 of a whole corpus file, with the checksum field itself read as zeros.
/// `file` must be at least one header long.
// NOLINTNEXTLINE(readability-identifier-naming)
[[nodiscard]] CorpusChecksum ComputeCorpusChecksum(std::span<const std::byte> file);

[[nodiscard]] std::expected<std::vector<CorpusChunk>, EngineError> readCorpusFile(const std::filesystem::path &path);

} // namespace nesso::core

#endif // CORE_CORPUS_INDEX_HPP

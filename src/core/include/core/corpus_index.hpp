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

/// Optional policy key naming the prompt-prefix rule and its version.
inline constexpr std::string_view EMBED_PREFIX_KEY = "embed.prefix";
/// Value of `EMBED_PREFIX_KEY` for a corpus written before the key existed.
inline constexpr std::string_view EMBED_PREFIX_UNRECORDED = "none";

/// `embed.prefix` of `policy`, or `EMBED_PREFIX_UNRECORDED` when the key is
/// absent.
[[nodiscard]] std::string_view EmbedPrefixOf(const CorpusPolicy &policy);

/// Optional policy key naming which part of each line was embedded and the
/// version of that rule (`parser::HEADER_RULE_VERSION`).
inline constexpr std::string_view EMBED_TEXT_KEY = "embed.text";
/// Value of `EMBED_TEXT_KEY` for a corpus written before the key existed: the
/// whole line was embedded.
inline constexpr std::string_view EMBED_TEXT_UNRECORDED = "line";

/// `embed.text` of `policy`, or `EMBED_TEXT_UNRECORDED` when the key is absent.
[[nodiscard]] std::string_view EmbedTextOf(const CorpusPolicy &policy);

struct LogChunk
{
    std::string text;
    std::uint64_t line_number = 0;
    std::string source;
    std::uint64_t byte_offset = 0;
    bool array_element = false;
    std::uint64_t byte_length = 0;
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
    std::int64_t modified_time = 0;
    bool tracked = false;
    std::string json_field = "message";
    bool stored_only = false;
};

struct CorpusLine
{
    std::uint64_t line_number = 0;
    std::uint64_t byte_offset = 0;
    std::uint64_t byte_length = 0;
    std::uint32_t source_index = 0;
    std::uint32_t distinct_id = NO_DISTINCT;
    bool array_element = false;
    std::optional<std::string_view> stored_text = std::nullopt;
};

/// One corpus line's BM25 postings: `|D|` and unique `(termId, tf)` pairs.
struct LexicalLinePostings
{
    std::uint32_t length = 0;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> entries;
};

/// Lexical readiness of a mapped corpus for hybrid/lexical/`auto` search.
/// Corrupt postings still fail at map time; only missing or stale policy is
/// soft.
enum class LexicalStatus : std::uint8_t
{
    Ok,
    Missing,
    VersionMismatch
};

/// Capture metadata used to detect changes before re-reading a source.
[[nodiscard]] std::expected<SourceInfo, EngineError> SourceInfoFor(const std::filesystem::path &path,
                                                                   std::string_view json_field);

/// Failure from `MapCorpusFile`. `version` is set when the header named a
/// format version, including one this build does not read.
struct CorpusMapFailure
{
    EngineError code = EngineError::CorruptDatabase;
    std::optional<std::uint32_t> version;
};

struct MappedCorpusFile;

namespace detail
{
struct CorpusWriterAccess;
} // namespace detail

/// Text and matrix views remain valid while the mapped corpus is alive.
class MappedCorpus
{
  public:
    ~MappedCorpus();
    MappedCorpus(const MappedCorpus &) = delete;
    MappedCorpus &operator=(const MappedCorpus &) = delete;
    MappedCorpus(MappedCorpus &&) noexcept;
    MappedCorpus &operator=(MappedCorpus &&) noexcept;

    [[nodiscard]] const CorpusPolicy &Policy() const noexcept;
    [[nodiscard]] std::uint32_t Dimensions() const noexcept;
    [[nodiscard]] std::span<const SourceInfo> Sources() const noexcept;
    [[nodiscard]] std::span<const CorpusLine> Lines() const noexcept;
    [[nodiscard]] std::span<const std::string_view> Texts() const noexcept;
    [[nodiscard]] std::span<const float> Matrix() const noexcept;
    /// Sorted unique lexical terms; ids in `LexicalPostings()` index this table.
    [[nodiscard]] std::span<const std::string_view> LexicalDictionary() const noexcept;
    /// One posting list per corpus line, including skipped lines.
    [[nodiscard]] std::span<const LexicalLinePostings> LexicalPostings() const noexcept;
    /// Soft lexical gate for non-dense modes. Dense search ignores this.
    [[nodiscard]] LexicalStatus GetLexicalStatus() const noexcept;
    /// Corpus `lexical.terms` policy value when present; empty when absent.
    [[nodiscard]] std::string_view CorpusLexicalTermsVersion() const noexcept;

  private:
    struct Impl;
    friend std::expected<MappedCorpusFile, CorpusMapFailure> MapCorpusFile(const std::filesystem::path &path);
    explicit MappedCorpus(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

/// Spills ordered line records and distinct texts/vectors before atomically
/// replacing the destination.
class CorpusWriter
{
  public:
    ~CorpusWriter();
    CorpusWriter(const CorpusWriter &) = delete;
    CorpusWriter &operator=(const CorpusWriter &) = delete;
    CorpusWriter(CorpusWriter &&) noexcept;
    CorpusWriter &operator=(CorpusWriter &&other) noexcept;

    [[nodiscard]] static std::expected<CorpusWriter, EngineError> Create(const std::filesystem::path &path,
                                                                         CorpusPolicy policy);
    [[nodiscard]] std::expected<std::uint32_t, EngineError> AddSource(SourceInfo source);
    /// Reserve a distinct id before its embedding is available, so lines can be
    /// streamed immediately.
    [[nodiscard]] std::expected<std::uint32_t, EngineError> ReserveDistinct(std::string_view text);
    /// Write vectors in distinct-id order. Every reserved id must have a vector
    /// before finish.
    [[nodiscard]] std::expected<void, EngineError> AddVector(std::uint32_t distinct_id,
                                                             std::span<const float> embedding);
    [[nodiscard]] std::expected<std::uint32_t, EngineError> AddDistinct(std::string_view text,
                                                                        std::span<const float> embedding);
    /// `lineTerms.size()` must equal `lines.size()`. Each entry is the term
    /// multiset for that line (`LexicalTerms`); skipped lines use an empty list.
    [[nodiscard]] std::expected<void, EngineError> AddLines(std::span<const CorpusLine> lines,
                                                            std::span<const std::vector<std::string>> line_terms);
    [[nodiscard]] std::expected<void, EngineError> Finish();

  private:
    friend struct detail::CorpusWriterAccess;
    struct Impl;
    explicit CorpusWriter(std::unique_ptr<Impl> impl);
    static void DiscardSpills(Impl *impl) noexcept;
    std::unique_ptr<Impl> impl_;
};

/// A mapped corpus and the format version stored in its header.
struct MappedCorpusFile
{
    MappedCorpus corpus;
    std::uint32_t version = 0;
};

/// Maps `path`. A header version is returned on success and on failure,
/// including when that version is not supported.
[[nodiscard]] std::expected<MappedCorpusFile, CorpusMapFailure> MapCorpusFile(const std::filesystem::path &path);

/// Expands distinct vectors for callers that need owned per-line chunks.
/// First 16 bytes of the SHA-256 of a whole corpus file, with the checksum
/// field itself read as zeros. `file` must be at least one header long.
[[nodiscard]] CorpusChecksum ComputeCorpusChecksum(std::span<const std::byte> file);

[[nodiscard]] std::expected<std::vector<CorpusChunk>, EngineError> ReadCorpusFile(const std::filesystem::path &path);

} // namespace nesso::core

#endif // CORE_CORPUS_INDEX_HPP

// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#ifndef NESSO_CORE_CORPUS_DETAIL_HPP
#define NESSO_CORE_CORPUS_DETAIL_HPP

#include <core/corpus_index.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <iosfwd>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace nesso::core::detail
{

inline constexpr std::uint64_t HEADER_BYTES = 128;
inline constexpr std::uint64_t SOURCE_RECORD_BYTES = 48;
inline constexpr std::uint64_t LINE_RECORD_BYTES = 56;
inline constexpr std::uint64_t TEXT_RECORD_BYTES = 16;
inline constexpr std::uint64_t MAX_POLICY_BYTES = 64ULL * 1024ULL;
inline constexpr std::uint64_t MAX_TEXT_BYTES = 8ULL * 1024ULL * 1024ULL;
inline constexpr std::uint64_t MAX_SOURCES = 1'000'000;
inline constexpr std::uint64_t MAX_LINES = 10'000'000;
inline constexpr std::uint64_t MAX_DISTINCT = 1'000'000;
inline constexpr std::uint64_t MAX_LEXICAL_TERMS = 4'000'000;
inline constexpr std::uint64_t MAX_LEXICAL_BYTES = 512ULL * 1024ULL * 1024ULL;
// Finish-time working set for unique-term runs, merge, and posting encode.
inline constexpr std::uint64_t CORPUS_WRITER_MEMORY_BUDGET_BYTES = 1536ULL * 1024ULL * 1024ULL;
inline constexpr std::uint32_t MAX_DIMENSIONS = 65536;

// Count caps the writer enforces. Production code uses the defaults; tests
// pass small values through CorpusWriterAccess so they need not build a corpus
// at the real caps.
struct CorpusLimits
{
    std::uint64_t sources = MAX_SOURCES;
    std::uint64_t lines = MAX_LINES;
    std::uint64_t distinct = MAX_DISTINCT;
    std::uint64_t lexical_terms = MAX_LEXICAL_TERMS;
    std::uint64_t lexical_bytes = MAX_LEXICAL_BYTES;
};

struct CorpusWriterAccess
{
    // CorpusWriter::Create with `limits` in place of the default caps.
    [[nodiscard]] static std::expected<CorpusWriter, EngineError>
    Create(const std::filesystem::path &path, CorpusPolicy policy, const CorpusLimits &limits);
};
inline constexpr std::array<char, 4> MAGIC{'N', 'E', 'S', 'C'};
inline constexpr std::uint32_t SOURCE_TRACKED = 1U;
inline constexpr std::uint32_t SOURCE_STORED_ONLY = 2U;
inline constexpr std::uint32_t LINE_ARRAY_ELEMENT = 1U;
inline constexpr std::uint32_t LINE_STORED_TEXT = 2U;
inline constexpr std::uint32_t LINE_SKIPPED = 4U;

[[nodiscard]] bool RangeInside(std::uint64_t offset, std::uint64_t length, std::uint64_t limit);
[[nodiscard]] std::uint64_t AlignUp(std::uint64_t value, std::uint64_t alignment);

[[nodiscard]] bool WriteRaw(std::ostream &output, const void *data, std::size_t size);
[[nodiscard]] bool WriteU32(std::ostream &output, std::uint32_t value);
[[nodiscard]] bool WriteU64(std::ostream &output, std::uint64_t value);
[[nodiscard]] bool ReadExact(std::istream &input, void *data, std::size_t size);
[[nodiscard]] bool ReadU32Stream(std::istream &input, std::uint32_t &value);
[[nodiscard]] bool ReadU64Stream(std::istream &input, std::uint64_t &value);
[[nodiscard]] bool ReadU32(std::span<const std::byte> bytes, std::uint64_t offset, std::uint32_t &value);
[[nodiscard]] bool ReadU64(std::span<const std::byte> bytes, std::uint64_t offset, std::uint64_t &value);
[[nodiscard]] std::string_view ViewBytes(std::span<const std::byte> bytes, std::uint64_t offset, std::uint64_t length);
[[nodiscard]] bool CopyBytes(std::istream &input, std::ostream &output, std::uint64_t bytes);
[[nodiscard]] bool WritePadding(std::ostream &output, std::uint64_t count);
[[nodiscard]] bool IsZero(std::span<const std::byte> bytes, std::uint64_t begin, std::uint64_t end);
[[nodiscard]] bool AtEnd(std::istream &input);

[[nodiscard]] std::expected<std::uint32_t, EngineError> ValidatePolicy(const CorpusPolicy &policy);
[[nodiscard]] LexicalStatus LexicalStatusOf(bool has_lexical, const CorpusPolicy &policy);
[[nodiscard]] std::expected<void, EngineError> ValidateVector(std::span<const float> embedding,
                                                              std::uint32_t dimensions);
[[nodiscard]] std::string SerializePolicy(const CorpusPolicy &policy);
[[nodiscard]] std::expected<CorpusPolicy, EngineError> ParsePolicy(std::string_view text);

[[nodiscard]] std::vector<std::byte> EncodeLexicalSection(std::span<const std::vector<std::string>> line_terms);

struct LexicalSpillFile
{
    std::filesystem::path path;
    std::uint64_t bytes = 0;
};

// Streams the terms spill into a lexical spill with bounded unique-term runs.
// Bytes match EncodeLexicalSection on the same term rows. memory_budget sizes
// the in-memory unique-term chunk (default CORPUS_WRITER_MEMORY_BUDGET_BYTES).
// More terms or bytes than `limits` allows is LimitsExceeded.
[[nodiscard]] std::expected<LexicalSpillFile, EngineError> EncodeLexicalSectionSpilled(
    const std::filesystem::path &terms_path, std::uint64_t line_count, const std::filesystem::path &spill_prefix,
    std::uint64_t memory_budget = CORPUS_WRITER_MEMORY_BUDGET_BYTES, const CorpusLimits &limits = {});

[[nodiscard]] std::expected<void, EngineError>
DecodeLexicalSection(std::span<const std::byte> bytes, std::uint64_t line_count, std::vector<std::string> &owned_terms,
                     std::vector<std::string_view> &dictionary, std::vector<LexicalLinePostings> &postings);

} // namespace nesso::core::detail

#endif // NESSO_CORE_CORPUS_DETAIL_HPP

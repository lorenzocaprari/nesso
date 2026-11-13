// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#ifndef NESSO_CORE_CORPUS_DETAIL_HPP
#define NESSO_CORE_CORPUS_DETAIL_HPP

#include <core/corpus_index.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
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
inline constexpr std::uint32_t MAX_DIMENSIONS = 65536;
inline constexpr std::array<char, 4> MAGIC{'N', 'E', 'S', 'C'};
inline constexpr std::uint32_t SOURCE_TRACKED = 1U;
inline constexpr std::uint32_t SOURCE_STORED_ONLY = 2U;
inline constexpr std::uint32_t LINE_ARRAY_ELEMENT = 1U;
inline constexpr std::uint32_t LINE_STORED_TEXT = 2U;
inline constexpr std::uint32_t LINE_SKIPPED = 4U;

[[nodiscard]] bool rangeInside(std::uint64_t offset, std::uint64_t length, std::uint64_t limit);
[[nodiscard]] std::uint64_t alignUp(std::uint64_t value, std::uint64_t alignment);

[[nodiscard]] bool writeRaw(std::ostream &output, const void *data, std::size_t size);
[[nodiscard]] bool writeU32(std::ostream &output, std::uint32_t value);
[[nodiscard]] bool writeU64(std::ostream &output, std::uint64_t value);
[[nodiscard]] bool readExact(std::istream &input, void *data, std::size_t size);
[[nodiscard]] bool readU32Stream(std::istream &input, std::uint32_t &value);
[[nodiscard]] bool readU64Stream(std::istream &input, std::uint64_t &value);
[[nodiscard]] bool readU32(std::span<const std::byte> bytes, std::uint64_t offset, std::uint32_t &value);
[[nodiscard]] bool readU64(std::span<const std::byte> bytes, std::uint64_t offset, std::uint64_t &value);
[[nodiscard]] std::string_view viewBytes(std::span<const std::byte> bytes, std::uint64_t offset, std::uint64_t length);
[[nodiscard]] bool copyBytes(std::istream &input, std::ostream &output, std::uint64_t bytes);
[[nodiscard]] bool writePadding(std::ostream &output, std::uint64_t count);
[[nodiscard]] bool isZero(std::span<const std::byte> bytes, std::uint64_t begin, std::uint64_t end);
[[nodiscard]] bool atEnd(std::istream &input);

[[nodiscard]] std::expected<std::uint32_t, EngineError> validatePolicy(const CorpusPolicy &policy);
[[nodiscard]] LexicalStatus lexicalStatusOf(bool hasLexical, const CorpusPolicy &policy);
[[nodiscard]] std::expected<void, EngineError> validateVector(std::span<const float> embedding,
                                                              std::uint32_t dimensions);
[[nodiscard]] std::string serializePolicy(const CorpusPolicy &policy);
[[nodiscard]] std::expected<CorpusPolicy, EngineError> parsePolicy(std::string_view text);

[[nodiscard]] std::vector<std::byte> encodeLexicalSection(std::span<const std::vector<std::string>> lineTerms);
[[nodiscard]] std::expected<void, EngineError>
decodeLexicalSection(std::span<const std::byte> bytes, std::uint64_t lineCount, std::vector<std::string> &ownedTerms,
                     std::vector<std::string_view> &dictionary, std::vector<LexicalLinePostings> &postings);

} // namespace nesso::core::detail

#endif // NESSO_CORE_CORPUS_DETAIL_HPP

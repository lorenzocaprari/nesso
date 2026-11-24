// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#ifndef PARSER_GZIP_READER_HPP
#define PARSER_GZIP_READER_HPP

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>

namespace nesso::parser
{

enum class GzipError : uint8_t
{
    OpenFailure,
    Corrupt
};

/// True when the final extension is `.gz`, compared without regard to case.
[[nodiscard]] auto IsGzipPath(const std::filesystem::path &path) -> bool;

/// Inflate one gzip file, including concatenated members, into a single byte
/// string. `OpenFailure` is a path that cannot be read. `Corrupt` is a stream
/// that is not a complete gzip member.
[[nodiscard]] auto ReadGzip(const std::filesystem::path &path) -> std::expected<std::string, GzipError>;

/// Inflate gzip bytes already in memory, including concatenated members.
/// `Corrupt` is a stream that is not a complete gzip member.
[[nodiscard]] auto ReadGzipBytes(std::string_view bytes) -> std::expected<std::string, GzipError>;

} // namespace nesso::parser

#endif // PARSER_GZIP_READER_HPP

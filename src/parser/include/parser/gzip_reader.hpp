// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#ifndef PARSER_GZIP_READER_HPP
#define PARSER_GZIP_READER_HPP

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>

namespace nesso::parser
{

enum class GzipError : uint8_t
{
    OpenFailure,
    Corrupt
};

/// True when the final extension is `.gz`, compared without regard to case.
[[nodiscard]] bool isGzipPath(const std::filesystem::path &path);

/// Inflate one gzip file, including concatenated members, into a single byte string.
/// `OpenFailure` is a path that cannot be read. `Corrupt` is a stream that is not a complete gzip member.
[[nodiscard]] std::expected<std::string, GzipError> readGzip(const std::filesystem::path &path);

} // namespace nesso::parser

#endif // PARSER_GZIP_READER_HPP

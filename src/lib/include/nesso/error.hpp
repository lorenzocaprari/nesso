// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#ifndef NESSO_ERROR_HPP
#define NESSO_ERROR_HPP

#include <cstddef>
#include <cstdint>
#include <filesystem>

namespace nesso
{

enum class ErrorKind : uint8_t
{
    EmbedderLoad,
    Parse,
    Embed,
    QueryEmbed,
    IndexBuild,
    Search,
    EmptyInput,
    NoMatches,
    CorpusRead,
    CorpusWrite,
    CorpusLoad,
    StoreInit,
    StoreOpen,
    StoreMissing,
    StoreSearch,
    InputOpen,
    QuerySize,
    QueryRead
};

/// Failure of a Nesso operation.
///
/// `code` is the underlying layer's error enum value. `path`, `skippedLines`, and `dimensions` are set when the
/// failure concerns a file, happens after parsing, or depends on a vector size.
struct Error
{
    ErrorKind kind{};
    int code = 0;
    std::filesystem::path path;
    size_t skippedLines = 0;
    uint64_t dimensions = 0;
};

} // namespace nesso

#endif // NESSO_ERROR_HPP

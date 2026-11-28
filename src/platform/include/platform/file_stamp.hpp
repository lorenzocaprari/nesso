// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#ifndef PLATFORM_FILE_STAMP_HPP
#define PLATFORM_FILE_STAMP_HPP

#include <cstdint>
#include <filesystem>
#include <optional>

namespace nesso::platform
{

/// Identity of a regular file for change detection (device, inode, size, times).
struct FileStamp
{
    std::uintmax_t device = 0;
    std::uintmax_t inode = 0;
    std::intmax_t size = 0;
    std::intmax_t modified_seconds = 0;
    std::intmax_t modified_nanoseconds = 0;
    std::intmax_t changed_seconds = 0;
    std::intmax_t changed_nanoseconds = 0;

    friend auto operator==(const FileStamp &, const FileStamp &) -> bool = default;
};

/// Stamp for `path`, or nullopt when the file cannot be stated.
[[nodiscard]] auto ReadFileStamp(const std::filesystem::path &path) -> std::optional<FileStamp>;

} // namespace nesso::platform

#endif // PLATFORM_FILE_STAMP_HPP

// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <platform/file_stamp.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <sys/stat.h>

namespace nesso::platform
{

auto ReadFileStamp(const std::filesystem::path &path) -> std::optional<FileStamp>
{
    struct stat status{};
    if (::stat(path.c_str(), &status) != 0)
    {
        return std::nullopt;
    }
    return FileStamp{.device = static_cast<std::uintmax_t>(status.st_dev),
                     .inode = static_cast<std::uintmax_t>(status.st_ino),
                     .size = static_cast<std::intmax_t>(status.st_size),
                     .modified_seconds = static_cast<std::intmax_t>(status.st_mtim.tv_sec),
                     .modified_nanoseconds = static_cast<std::intmax_t>(status.st_mtim.tv_nsec),
                     .changed_seconds = static_cast<std::intmax_t>(status.st_ctim.tv_sec),
                     .changed_nanoseconds = static_cast<std::intmax_t>(status.st_ctim.tv_nsec)};
}

} // namespace nesso::platform

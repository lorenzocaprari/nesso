// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <platform/file_stamp.hpp>

#include <cstdint>
#include <optional>
#include <windows.h>

namespace nesso::platform
{

namespace
{

auto FileTimeToUnixParts(FILETIME file_time, std::intmax_t &seconds, std::intmax_t &nanoseconds) -> void
{
    // FILETIME is 100ns intervals since 1601-01-01; Unix epoch is 1970-01-01.
    constexpr std::uint64_t EPOCH_DIFF = 11644473600ULL;
    constexpr std::uint64_t HUNDRED_NS_PER_SEC = 10000000ULL;
    const std::uint64_t ticks = (static_cast<std::uint64_t>(file_time.dwHighDateTime) << 32U) |
                                static_cast<std::uint64_t>(file_time.dwLowDateTime);
    const std::uint64_t unix_ticks = ticks / HUNDRED_NS_PER_SEC;
    if (unix_ticks < EPOCH_DIFF)
    {
        seconds = 0;
        nanoseconds = 0;
        return;
    }
    seconds = static_cast<std::intmax_t>(unix_ticks - EPOCH_DIFF);
    nanoseconds = static_cast<std::intmax_t>((ticks % HUNDRED_NS_PER_SEC) * 100);
}

} // namespace

auto ReadFileStamp(const std::filesystem::path &path) -> std::optional<FileStamp>
{
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        return std::nullopt;
    }
    BY_HANDLE_FILE_INFORMATION info{};
    const BOOL ok = GetFileInformationByHandle(file, &info);
    FILE_BASIC_INFO basic{};
    // Prefer ChangeTime (metadata updates) over CreationTime; an in-place
    // rewrite can keep CreationTime and restore LastWriteTime.
    const BOOL basic_ok = GetFileInformationByHandleEx(file, FileBasicInfo, &basic, sizeof(basic));
    CloseHandle(file);
    if (ok == 0)
    {
        return std::nullopt;
    }
    FileStamp stamp{};
    stamp.device = static_cast<std::uintmax_t>(info.dwVolumeSerialNumber);
    stamp.inode =
        (static_cast<std::uintmax_t>(info.nFileIndexHigh) << 32U) | static_cast<std::uintmax_t>(info.nFileIndexLow);
    stamp.size = (static_cast<std::intmax_t>(info.nFileSizeHigh) << 32) | static_cast<std::intmax_t>(info.nFileSizeLow);
    FileTimeToUnixParts(info.ftLastWriteTime, stamp.modified_seconds, stamp.modified_nanoseconds);
    if (basic_ok != 0)
    {
        FILETIME change{};
        change.dwLowDateTime = static_cast<DWORD>(basic.ChangeTime.LowPart);
        change.dwHighDateTime = static_cast<DWORD>(basic.ChangeTime.HighPart);
        FileTimeToUnixParts(change, stamp.changed_seconds, stamp.changed_nanoseconds);
    }
    else
    {
        FileTimeToUnixParts(info.ftLastWriteTime, stamp.changed_seconds, stamp.changed_nanoseconds);
    }
    return stamp;
}

} // namespace nesso::platform

// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <platform/file.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <windows.h>
// psapi.h needs windows.h first (Win32 types).
#include <psapi.h>

namespace nesso::platform
{

MappedFile::~MappedFile()
{
    if (data_ != nullptr)
    {
        UnmapViewOfFile(data_);
    }
}

MappedFile::MappedFile(MappedFile &&other) noexcept : data_(other.data_), size_(other.size_)
{
    other.data_ = nullptr;
    other.size_ = 0;
}

auto MappedFile::operator=(MappedFile &&other) noexcept -> MappedFile &
{
    if (this != &other)
    {
        if (data_ != nullptr)
        {
            UnmapViewOfFile(data_);
        }
        data_ = other.data_;
        size_ = other.size_;
        other.data_ = nullptr;
        other.size_ = 0;
    }
    return *this;
}

auto MappedFile::Open(const std::filesystem::path &path) -> std::expected<MappedFile, FileError>
{
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        return std::unexpected(FileError::OpenFailure);
    }
    LARGE_INTEGER length{};
    if (GetFileSizeEx(file, &length) == 0)
    {
        CloseHandle(file);
        return std::unexpected(FileError::MapFailure);
    }
    // Compose from HighPart/LowPart; do not read QuadPart (cpp:S6232).
    if (length.HighPart < 0)
    {
        CloseHandle(file);
        return std::unexpected(FileError::MapFailure);
    }
    const auto length_bytes = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(length.HighPart)) << 32U) |
                              static_cast<std::uint64_t>(static_cast<std::uint32_t>(length.LowPart));
    MappedFile mapped;
    mapped.size_ = static_cast<std::size_t>(length_bytes);
    if (mapped.size_ == 0)
    {
        CloseHandle(file);
        return mapped;
    }
    const HANDLE mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    CloseHandle(file);
    if (mapping == nullptr)
    {
        return std::unexpected(FileError::MapFailure);
    }
    void *const view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
    CloseHandle(mapping);
    if (view == nullptr)
    {
        return std::unexpected(FileError::MapFailure);
    }
    mapped.data_ = static_cast<std::byte *>(view);
    return mapped;
}

auto MappedFile::Bytes() const noexcept -> std::span<const std::byte> { return {data_, size_}; }

auto ReplaceFileAtomically(const std::filesystem::path &source, const std::filesystem::path &destination)
    -> std::expected<void, FileError>
{
    if (MoveFileExW(source.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0)
    {
        return std::unexpected(FileError::ReplaceFailure);
    }
    return {};
}

auto PeakResidentBytes() noexcept -> std::int64_t
{
    PROCESS_MEMORY_COUNTERS counters{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)) == 0)
    {
        return 0;
    }
    return static_cast<std::int64_t>(counters.PeakWorkingSetSize);
}

} // namespace nesso::platform

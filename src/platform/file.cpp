// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <platform/file.hpp>

#include <bits/types/struct_rusage.h>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <fcntl.h>
#include <filesystem>
#include <span>
#include <sys/mman.h>
#include <sys/resource.h>
#include <unistd.h>

namespace nesso::platform
{

MappedFile::~MappedFile()
{
    if (data_ != nullptr)
    {
        munmap(data_, size_);
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
            munmap(data_, size_);
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
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg, hicpp-vararg)
    const int file_descriptor = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (file_descriptor < 0)
    {
        return std::unexpected(FileError::OpenFailure);
    }
    const auto length = ::lseek(file_descriptor, 0, SEEK_END);
    if (length < 0)
    {
        ::close(file_descriptor);
        return std::unexpected(FileError::MapFailure);
    }
    MappedFile mapped;
    mapped.size_ = static_cast<std::size_t>(length);
    if (mapped.size_ == 0)
    {
        ::close(file_descriptor);
        return mapped;
    }
    void *const view = ::mmap(nullptr, mapped.size_, PROT_READ, MAP_PRIVATE, file_descriptor, 0);
    ::close(file_descriptor);
    if (view == MAP_FAILED)
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
    if (::rename(source.c_str(), destination.c_str()) != 0)
    {
        return std::unexpected(FileError::ReplaceFailure);
    }
    return {};
}

auto PeakResidentBytes() noexcept -> std::int64_t
{
    rusage usage{};
    const int status = ::getrusage(RUSAGE_SELF, &usage);
    // glibc exposes ru_maxrss only as an anonymous-union member, in kilobytes.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
    const auto peak_kb = status == 0 ? usage.ru_maxrss : 0;
    return static_cast<std::int64_t>(peak_kb) * 1024;
}

} // namespace nesso::platform

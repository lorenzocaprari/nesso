// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <platform/file.hpp>

#include <sys/mman.h>
#include <sys/resource.h>
#include <unistd.h>

#include <fcntl.h>

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

MappedFile &MappedFile::operator=(MappedFile &&other) noexcept
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

std::expected<MappedFile, FileError> MappedFile::open(const std::filesystem::path &path)
{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg, hicpp-vararg)
    const int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0)
    {
        return std::unexpected(FileError::OpenFailure);
    }
    const off_t length = ::lseek(fd, 0, SEEK_END);
    if (length < 0)
    {
        ::close(fd);
        return std::unexpected(FileError::MapFailure);
    }
    MappedFile mapped;
    mapped.size_ = static_cast<std::size_t>(length);
    if (mapped.size_ == 0)
    {
        ::close(fd);
        return mapped;
    }
    void *const view = ::mmap(nullptr, mapped.size_, PROT_READ, MAP_PRIVATE, fd, 0);
    ::close(fd);
    if (view == MAP_FAILED)
    {
        return std::unexpected(FileError::MapFailure);
    }
    mapped.data_ = static_cast<std::byte *>(view);
    return mapped;
}

std::span<const std::byte> MappedFile::bytes() const noexcept { return {data_, size_}; }

std::expected<void, FileError> replaceFileAtomically(const std::filesystem::path &source,
                                                     const std::filesystem::path &destination)
{
    if (::rename(source.c_str(), destination.c_str()) != 0)
    {
        return std::unexpected(FileError::ReplaceFailure);
    }
    return {};
}

std::int64_t peakResidentBytes() noexcept
{
    rusage usage{};
    const int status = ::getrusage(RUSAGE_SELF, &usage);
    // glibc exposes ru_maxrss only as an anonymous-union member, in kilobytes.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
    const auto peakKb = status == 0 ? usage.ru_maxrss : 0;
    return static_cast<std::int64_t>(peakKb) * 1024;
}

} // namespace nesso::platform

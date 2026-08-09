// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT
#include "include/mach_core/storage_engine.hpp"

#include <algorithm>
#include <array>
#include <fcntl.h>
#include <fstream>
#include <limits>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace mach_core
{

static constexpr std::array<uint8_t, 4> MAGIC{{'M', 'A', 'C', 'H'}};
static constexpr uint32_t SUPPORTED_VERSION = 1;

template <SupportedScalar T>
[[nodiscard]] static bool payloadSizeBytes(uint64_t vectorCount, uint64_t dimensions, size_t &outBytes) noexcept
{
    if (dimensions == 0)
    {
        return false;
    }

    const size_t elementBytes = sizeof(T);
    if (dimensions > (std::numeric_limits<size_t>::max() / elementBytes))
    {
        return false;
    }
    const size_t rowBytes = static_cast<size_t>(dimensions) * elementBytes;
    if (vectorCount > 0 && vectorCount > (std::numeric_limits<size_t>::max() / rowBytes))
    {
        return false;
    }
    const size_t payloadBytes = static_cast<size_t>(vectorCount) * rowBytes;
    if (payloadBytes > (std::numeric_limits<size_t>::max() - sizeof(DatabaseHeader)))
    {
        return false;
    }
    outBytes = sizeof(DatabaseHeader) + payloadBytes;
    return true;
}

// --- Move Semantics (Transferring resource ownership cleanly) ---
template <SupportedScalar T> StorageEngine<T>::StorageEngine(StorageEngine &&other) noexcept
{
    *this = std::move(other);
}

template <SupportedScalar T> StorageEngine<T> &StorageEngine<T>::operator=(StorageEngine &&other) noexcept
{
    if (this != &other)
    {
        close(); // Discard local resources first
        m_fd = other.m_fd;
        m_mapped_size = other.m_mapped_size;
        m_raw_mmap = other.m_raw_mmap;
        m_header = other.m_header;
        m_vector_data_pool = other.m_vector_data_pool;

        // Reset the proxy object so its destructor doesn't clear the file we
        // just grabbed
        other.m_fd = -1;
        other.m_raw_mmap = nullptr;
        other.m_header = nullptr;
        other.m_vector_data_pool = nullptr;
    }
    return *this;
}

// --- The RAII Core Closer ---
template <SupportedScalar T> void StorageEngine<T>::close() noexcept
{
    if (m_raw_mmap != nullptr)
    {
        ::munmap(m_raw_mmap, m_mapped_size);
        m_raw_mmap = nullptr;
    }
    if (m_fd != -1)
    {
        ::close(m_fd);
        m_fd = -1;
    }
    m_header = nullptr;
    m_vector_data_pool = nullptr;
    m_mapped_size = 0;
}

// --- Mount / Creation Sequence ---
template <SupportedScalar T>
std::expected<void, EngineError> StorageEngine<T>::createOrOpen(const std::filesystem::path &path,
                                                                uint64_t dimensions) noexcept
{
    close(); // Enforce reset protection

    if (dimensions == 0)
    {
        return std::unexpected(EngineError::MismatchedDimensions);
    }

    const bool isNewFile = !std::filesystem::exists(path);
    if (isNewFile)
    {
        const std::ofstream createFile(path, std::ios::binary);
        if (!createFile)
        {
            return std::unexpected(EngineError::FileOpenFailure);
        }
    }

    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg) memory-mapped engine, we must use the POSIX
    m_fd = ::open(path.c_str(), O_RDWR);
    if (m_fd == -1)
    {
        return std::unexpected(EngineError::FileOpenFailure);
    }

    struct stat st{};
    if (::fstat(m_fd, &st) == -1)
    {
        close();
        return std::unexpected(EngineError::FileOpenFailure);
    }

    size_t targetSize = sizeof(DatabaseHeader);
    if (!isNewFile)
    {
        if (std::cmp_less(st.st_size, sizeof(DatabaseHeader)))
        {
            close();
            return std::unexpected(EngineError::CorruptDatabase);
        }
        targetSize = static_cast<size_t>(st.st_size);
    }
    else if (::ftruncate(m_fd, static_cast<off_t>(targetSize)) == -1)
    {
        close();
        return std::unexpected(EngineError::FileResizeFailure);
    }
    m_mapped_size = targetSize;

    m_raw_mmap = static_cast<uint8_t *>(::mmap(nullptr, m_mapped_size, PROT_READ | PROT_WRITE, MAP_SHARED, m_fd, 0));
    if (m_raw_mmap == MAP_FAILED)
    {
        m_raw_mmap = nullptr;
        close();
        return std::unexpected(EngineError::MmapMappingFailure);
    }

    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) We are intentionally overlaying a struct on raw bytes
    m_header = reinterpret_cast<DatabaseHeader *>(m_raw_mmap);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) We are intentionally overlaying a struct on raw bytes
    m_vector_data_pool = reinterpret_cast<T *>(m_raw_mmap + sizeof(DatabaseHeader));

    if (isNewFile)
    {
        m_header->magic = MAGIC;
        m_header->version = SUPPORTED_VERSION;
        m_header->dimensions = dimensions;
        m_header->vector_count = 0;
        return {};
    }

    if (m_header->magic != MAGIC || m_header->version != SUPPORTED_VERSION)
    {
        close();
        return std::unexpected(EngineError::CorruptDatabase);
    }
    if (m_header->dimensions != dimensions)
    {
        close();
        return std::unexpected(EngineError::MismatchedDimensions);
    }

    size_t requiredBytes = 0;
    if (!payloadSizeBytes<T>(m_header->vector_count, m_header->dimensions, requiredBytes) ||
        requiredBytes > m_mapped_size)
    {
        close();
        return std::unexpected(EngineError::CorruptDatabase);
    }

    return {};
}

// --- High Performance Append System ---
template <SupportedScalar T>
std::expected<void, EngineError> StorageEngine<T>::appendVector(std::span<const T> vector) noexcept
{
    if (m_header == nullptr) [[unlikely]]
    {
        return std::unexpected(EngineError::DatabaseNotInitialized);
    }
    if (vector.size() != m_header->dimensions) [[unlikely]]
    {
        return std::unexpected(EngineError::MismatchedDimensions);
    }
    if (m_header->vector_count == std::numeric_limits<uint64_t>::max()) [[unlikely]]
    {
        return std::unexpected(EngineError::FileResizeFailure);
    }

    const uint64_t nextIndex = m_header->vector_count;
    size_t requiredSize = 0;
    if (!payloadSizeBytes<T>(nextIndex + 1, m_header->dimensions, requiredSize))
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }

    if (requiredSize > m_mapped_size)
    {
        ::munmap(m_raw_mmap, m_mapped_size);

        size_t newAllocatedScale = m_mapped_size;
        if (newAllocatedScale > (std::numeric_limits<size_t>::max() / 2))
        {
            newAllocatedScale = requiredSize;
        }
        else
        {
            newAllocatedScale = std::max(newAllocatedScale * 2, requiredSize);
        }

        if (::ftruncate(m_fd, static_cast<off_t>(newAllocatedScale)) == -1)
        {
            m_raw_mmap = nullptr;
            close();
            return std::unexpected(EngineError::FileResizeFailure);
        }

        m_raw_mmap =
            static_cast<uint8_t *>(::mmap(nullptr, newAllocatedScale, PROT_READ | PROT_WRITE, MAP_SHARED, m_fd, 0));
        if (m_raw_mmap == MAP_FAILED)
        {
            m_raw_mmap = nullptr;
            close();
            return std::unexpected(EngineError::MmapMappingFailure);
        }

        m_mapped_size = newAllocatedScale;

        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) We are intentionally overlaying a struct on raw
        m_header = reinterpret_cast<DatabaseHeader *>(m_raw_mmap);
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) We are intentionally overlaying a struct on raw
        m_vector_data_pool = reinterpret_cast<T *>(m_raw_mmap + sizeof(DatabaseHeader));
    }

    T *writeTarget = m_vector_data_pool + (nextIndex * m_header->dimensions);
    std::copy(vector.begin(), vector.end(), writeTarget);

    m_header->vector_count++;
    return {};
}

// --- Zero-Allocation Vector Fetching View ---
template <SupportedScalar T>
std::expected<std::span<const T>, EngineError> StorageEngine<T>::getVector(uint64_t index) const noexcept
{
    if (m_header == nullptr) [[unlikely]]
    {
        return std::unexpected(EngineError::DatabaseNotInitialized);
    }
    if (index >= m_header->vector_count) [[unlikely]]
    {
        return std::unexpected(EngineError::IndexOutOfBounds);
    }

    size_t endOffset = 0;
    if (!payloadSizeBytes<T>(index + 1, m_header->dimensions, endOffset) || endOffset > m_mapped_size)
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }

    const T *readSource = m_vector_data_pool + (index * m_header->dimensions);
    return std::span<const T>(readSource, m_header->dimensions);
}

template class StorageEngine<float>;
template class StorageEngine<double>;

} // namespace mach_core

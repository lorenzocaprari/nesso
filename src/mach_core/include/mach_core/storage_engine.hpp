// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT
#ifndef MACH_CORE_STORAGE_ENGINE_HPP
#define MACH_CORE_STORAGE_ENGINE_HPP

#include "core_types.hpp"
#include <expected>
#include <filesystem>
#include <span>

namespace mach_core
{

template <SupportedScalar T> class StorageEngine
{
  public:
    // Constructors & Destructor
    explicit StorageEngine() noexcept = default;
    ~StorageEngine() noexcept { close(); }

    // Enforce RAII: Resources are single-ownership. Non-copyable, but movable.
    StorageEngine(const StorageEngine &) = delete;
    StorageEngine &operator=(const StorageEngine &) = delete;
    StorageEngine(StorageEngine &&other) noexcept;
    StorageEngine &operator=(StorageEngine &&other) noexcept;

    // Core Lifecycle API
    [[nodiscard]] std::expected<void, EngineError> createOrOpen(const std::filesystem::path &path,
                                                                uint64_t dimensions) noexcept;
    void close() noexcept;

    // Database Actions (Zero-Allocation Paths)
    [[nodiscard]] std::expected<void, EngineError> appendVector(std::span<const T> vector) noexcept;
    [[nodiscard]] std::expected<std::span<const T>, EngineError> getVector(uint64_t index) const noexcept;

    [[nodiscard]] bool isOpen() const noexcept { return m_header != nullptr; }
    [[nodiscard]] uint64_t getVectorCount() const noexcept { return m_header != nullptr ? m_header->vector_count : 0; }
    [[nodiscard]] uint64_t getDimensions() const noexcept { return m_header != nullptr ? m_header->dimensions : 0; }
    [[nodiscard]] const T *vectorData() const noexcept { return m_vector_data_pool; }

  private:
    int m_fd{-1};                      // Linux native file descriptor
    size_t m_mapped_size{0};           // Current capacity of virtual memory space
    uint8_t *m_raw_mmap{nullptr};      // Base address of memory mapping
    DatabaseHeader *m_header{nullptr}; // Pointer overlaying the file head mapping
    T *m_vector_data_pool{nullptr};    // Pointer mapping the start of contiguous vectors
};

} // namespace mach_core

#endif // MACH_CORE_STORAGE_ENGINE_HPP

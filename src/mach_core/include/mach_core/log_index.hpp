// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT
#ifndef MACH_CORE_LOG_INDEX_HPP
#define MACH_CORE_LOG_INDEX_HPP

#include "bm25_index.hpp"
#include "embedder.hpp"
#include "log_line_store.hpp"
#include "storage_engine.hpp"

#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace mach_core
{

struct HybridHit
{
    uint64_t index{0};
    uint64_t lineNo{0};
    float rrfScore{0.0f};
    std::string text;
};

class LogIndex
{
  public:
    [[nodiscard]] std::expected<void, EngineError> open(const std::filesystem::path &dbPath);
    void close() noexcept;

    [[nodiscard]] std::expected<uint64_t, EngineError> ingestFile(const std::filesystem::path &logFile,
                                                                  const IEmbedder &embedder, bool parallel);
    [[nodiscard]] std::expected<std::vector<HybridHit>, EngineError> search(std::string_view query, size_t k,
                                                                            const IEmbedder &embedder) const;

    [[nodiscard]] uint64_t vectorCount() const noexcept { return m_engine.getVectorCount(); }
    [[nodiscard]] bool isOpen() const noexcept { return m_engine.isOpen(); }

  private:
    [[nodiscard]] std::expected<uint64_t, EngineError> ingestSync(const std::filesystem::path &logFile,
                                                                  const IEmbedder &embedder);
    [[nodiscard]] std::expected<uint64_t, EngineError> ingestParallel(const std::filesystem::path &logFile,
                                                                      const IEmbedder &embedder);
    [[nodiscard]] std::expected<void, EngineError> appendIndexed(uint64_t lineNo, std::string_view text,
                                                                 std::span<const float> embedding);

    StorageEngine<float> m_engine;
    LogLineStore m_lines;
    Bm25Index m_bm25;
};

} // namespace mach_core

#endif // MACH_CORE_LOG_INDEX_HPP

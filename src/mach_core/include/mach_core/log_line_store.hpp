// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT
#ifndef MACH_CORE_LOG_LINE_STORE_HPP
#define MACH_CORE_LOG_LINE_STORE_HPP

#include "core_types.hpp"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace mach_core
{

struct LogLine
{
    uint64_t id{0};
    uint64_t lineNo{0};
    std::string text;
};

[[nodiscard]] std::filesystem::path sidecarPathFor(const std::filesystem::path &dbPath);

class LogLineStore
{
  public:
    [[nodiscard]] std::expected<void, EngineError> open(const std::filesystem::path &path);
    void close() noexcept;

    [[nodiscard]] std::expected<void, EngineError> append(uint64_t id, uint64_t lineNo, std::string_view text);
    [[nodiscard]] std::expected<LogLine, EngineError> get(uint64_t id) const;
    [[nodiscard]] std::expected<std::vector<LogLine>, EngineError> loadAll() const;
    [[nodiscard]] uint64_t size() const noexcept { return m_lines.size(); }
    [[nodiscard]] bool isOpen() const noexcept { return !m_path.empty(); }

  private:
    [[nodiscard]] std::expected<void, EngineError> reload();

    std::filesystem::path m_path;
    std::vector<LogLine> m_lines;
};

} // namespace mach_core

#endif // MACH_CORE_LOG_LINE_STORE_HPP

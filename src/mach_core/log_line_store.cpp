// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT
#include "include/mach_core/log_line_store.hpp"

#include <fstream>
#include <nlohmann/json.hpp>

namespace mach_core
{

std::filesystem::path sidecarPathFor(const std::filesystem::path &dbPath)
{
    auto path = dbPath;
    path += ".lines.jsonl";
    return path;
}

std::expected<void, EngineError> LogLineStore::open(const std::filesystem::path &path)
{
    close();
    m_path = path;
    if (!std::filesystem::exists(m_path))
    {
        std::ofstream create(m_path);
        if (!create)
        {
            m_path.clear();
            return std::unexpected(EngineError::SidecarIoFailure);
        }
    }
    return reload();
}

void LogLineStore::close() noexcept
{
    m_path.clear();
    m_lines.clear();
}

std::expected<void, EngineError> LogLineStore::reload()
{
    m_lines.clear();
    std::ifstream in(m_path);
    if (!in)
    {
        return std::unexpected(EngineError::SidecarIoFailure);
    }

    std::string line;
    while (std::getline(in, line))
    {
        if (line.empty())
        {
            continue;
        }
        try
        {
            const auto doc = nlohmann::json::parse(line);
            LogLine entry;
            entry.id = doc.at("id").get<uint64_t>();
            entry.lineNo = doc.at("line_no").get<uint64_t>();
            entry.text = doc.at("text").get<std::string>();
            if (entry.id != m_lines.size())
            {
                return std::unexpected(EngineError::CorruptDatabase);
            }
            m_lines.push_back(std::move(entry));
        }
        catch (const nlohmann::json::exception &)
        {
            return std::unexpected(EngineError::CorruptDatabase);
        }
    }
    return {};
}

std::expected<void, EngineError> LogLineStore::append(uint64_t id, uint64_t lineNo, std::string_view text)
{
    if (m_path.empty())
    {
        return std::unexpected(EngineError::DatabaseNotInitialized);
    }
    if (id != m_lines.size())
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }

    nlohmann::json doc = {{"id", id}, {"line_no", lineNo}, {"text", text}};
    std::ofstream out(m_path, std::ios::app);
    if (!out)
    {
        return std::unexpected(EngineError::SidecarIoFailure);
    }
    out << doc.dump() << '\n';
    if (!out)
    {
        return std::unexpected(EngineError::SidecarIoFailure);
    }

    m_lines.push_back(LogLine{.id = id, .lineNo = lineNo, .text = std::string(text)});
    return {};
}

std::expected<LogLine, EngineError> LogLineStore::get(uint64_t id) const
{
    if (m_path.empty())
    {
        return std::unexpected(EngineError::DatabaseNotInitialized);
    }
    if (id >= m_lines.size())
    {
        return std::unexpected(EngineError::IndexOutOfBounds);
    }
    return m_lines[id];
}

std::expected<std::vector<LogLine>, EngineError> LogLineStore::loadAll() const
{
    if (m_path.empty())
    {
        return std::unexpected(EngineError::DatabaseNotInitialized);
    }
    return m_lines;
}

} // namespace mach_core

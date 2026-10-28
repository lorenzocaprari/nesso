// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <render_json.hpp>

#include <nlohmann/json.hpp>

#include <array>
#include <charconv>
#include <format>
#include <string>
#include <string_view>

namespace nesso::cli
{

static constexpr int SCHEMA_VERSION = 1;

// nlohmann stores a JSON number as double. Promoting the match float first
// prints the widened value (0.1 becomes 0.10000000149011612). to_chars on the
// float is the shortest round-trip and does not read the locale.
static std::string formatScore(float score)
{
    std::array<char, 64> buffer{};
    const std::to_chars_result result =
        std::to_chars(buffer.data(), buffer.data() + buffer.size(), score, std::chars_format::general);
    return {buffer.data(), static_cast<std::size_t>(result.ptr - buffer.data())};
}

static std::string dumpString(std::string_view text)
{
    return nlohmann::json(std::string{text}).dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

std::string renderMatchJson(const TextMatch &match, std::size_t rank)
{
    std::string out;
    out.reserve(match.source.size() + match.text.size() + 96);
    out.append("{\"schema_version\":");
    out.append(std::format("{}", SCHEMA_VERSION));
    out.append(",\"rank\":");
    out.append(std::format("{}", rank));
    out.append(",\"source\":");
    out.append(dumpString(match.source));
    out.append(",\"kind\":");
    out.append(dumpString(match.arrayElement ? "element" : "line"));
    out.append(",\"line\":");
    out.append(std::format("{}", match.line));
    out.append(",\"score\":");
    out.append(formatScore(match.score));
    out.append(",\"text\":");
    out.append(dumpString(match.text));
    out.append(",\"occurrences\":");
    out.append(std::format("{}", match.occurrences));
    out.push_back('}');
    return out;
}

} // namespace nesso::cli

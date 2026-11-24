// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include "nesso/nesso.hpp"
#include "nesso/overview.hpp"
#include <cli/render_json.hpp>

#include <cstddef>
#include <memory>
#include <nlohmann/json.hpp>

#include <array>
#include <charconv>
#include <format>
#include <optional>
#include <string>
#include <string_view>

namespace nesso::cli
{

static constexpr int SCHEMA_VERSION = 2;

// nlohmann stores a JSON number as double. Promoting the match float first
// prints the widened value (0.1 becomes 0.10000000149011612). to_chars on the
// float is the shortest round-trip and does not read the locale.
static auto FormatScore(float score) -> std::string
{
    std::array<char, 64> buffer{};
    const std::to_chars_result result =
        std::to_chars(buffer.data(), std::to_address(buffer.end()), score, std::chars_format::general);
    return {buffer.data(), static_cast<std::size_t>(result.ptr - buffer.data())};
}

static auto DumpString(std::string_view text) -> std::string
{
    return nlohmann::json(std::string{text}).dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

static void AppendOptionalFloat(std::string &out, std::optional<float> value)
{
    if (!value)
    {
        out.append("null");
        return;
    }
    out.append(FormatScore(*value));
}

auto RenderMatchJson(const TextMatch &match, std::size_t rank) -> std::string
{
    std::string out;
    out.reserve(match.source.size() + match.text.size() + 128);
    out.append("{\"schema_version\":");
    out.append(std::format("{}", SCHEMA_VERSION));
    out.append(",\"rank\":");
    out.append(std::format("{}", rank));
    out.append(",\"source\":");
    out.append(DumpString(match.source));
    out.append(",\"kind\":");
    out.append(DumpString(match.array_element ? "element" : "line"));
    out.append(",\"line\":");
    out.append(std::format("{}", match.line));
    out.append(",\"score\":");
    out.append(FormatScore(match.score));
    out.append(",\"text\":");
    out.append(DumpString(match.text));
    out.append(",\"occurrences\":");
    out.append(std::format("{}", match.occurrences));
    out.append(",\"mode\":");
    out.append(DumpString(match.mode));
    out.append(",\"dense\":");
    AppendOptionalFloat(out, match.dense);
    out.append(",\"lexical\":");
    AppendOptionalFloat(out, match.lexical);
    out.push_back('}');
    return out;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): rank and refresh are both counts from 1.
auto RenderProvisionalMatchJson(const TextMatch &match, std::size_t rank, std::size_t refresh) -> std::string
{
    std::string out = RenderMatchJson(match, rank);
    out.pop_back();
    out.append(R"(,"final":false,"refresh":)");
    out.append(std::format("{}", refresh));
    out.push_back('}');
    return out;
}

auto RenderEventTypeJson(const OverviewResults &results, const EventTypeRow &row, std::size_t rank) -> std::string
{
    std::string out;
    out.reserve(row.source.size() + row.text.size() + row.key.size() + 192);
    out.append("{\"schema_version\":");
    out.append(std::format("{}", SCHEMA_VERSION));
    out.append(R"(,"record":"event_type","rank":)");
    out.append(std::format("{}", rank));
    out.append(",\"count\":");
    out.append(std::format("{}", row.count));
    out.append(",\"source\":");
    out.append(DumpString(row.source));
    out.append(",\"kind\":");
    out.append(DumpString(row.array_element ? "element" : "line"));
    out.append(",\"line\":");
    out.append(std::format("{}", row.line));
    out.append(",\"text\":");
    out.append(DumpString(row.text));
    out.append(",\"key\":");
    out.append(DumpString(row.key));
    out.append(",\"first_seen\":");
    out.append(std::format("{}", row.first_seen));
    out.append(",\"last_seen\":");
    out.append(std::format("{}", row.last_seen));
    out.append(",\"lines\":");
    out.append(std::format("{}", results.lines));
    out.append(",\"types\":");
    out.append(std::format("{}", results.types));
    out.push_back('}');
    return out;
}

} // namespace nesso::cli

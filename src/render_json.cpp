// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <render_json.hpp>

#include <cstdint>
#include <format>
#include <string_view>

namespace nesso::cli
{

static constexpr int SCHEMA_VERSION = 1;

static void appendControl(std::string &out, unsigned char byte)
{
    switch (byte)
    {
    case '"':
        out.append("\\\"");
        return;
    case '\\':
        out.append("\\\\");
        return;
    case '\b':
        out.append("\\b");
        return;
    case '\f':
        out.append("\\f");
        return;
    case '\n':
        out.append("\\n");
        return;
    case '\r':
        out.append("\\r");
        return;
    case '\t':
        out.append("\\t");
        return;
    default:
        break;
    }

    const unsigned high = byte >> 4U;
    const unsigned low = byte & 0x0FU;
    out.append("\\u00");
    out.push_back(static_cast<char>('0' + high));
    if (low < 10U)
    {
        out.push_back(static_cast<char>('0' + low));
    }
    else
    {
        out.push_back(static_cast<char>('a' + (low - 10U)));
    }
}

// Rejects overlong forms, surrogates, and code points above U+10FFFF. On failure the caller
// consumes one byte, which is the maximal well-formed subpart of an ill-formed sequence.
static bool appendScalar(std::string &out, std::string_view text, std::size_t &index)
{
    const auto lead = static_cast<unsigned char>(text[index]);
    std::size_t need = 0;
    std::uint32_t code = 0;
    std::uint32_t minimum = 0;
    if ((lead & 0xE0U) == 0xC0U)
    {
        need = 2;
        code = lead & 0x1FU;
        minimum = 0x80;
    }
    else if ((lead & 0xF0U) == 0xE0U)
    {
        need = 3;
        code = lead & 0x0FU;
        minimum = 0x800;
    }
    else if ((lead & 0xF8U) == 0xF0U)
    {
        need = 4;
        code = lead & 0x07U;
        minimum = 0x10000;
    }
    else
    {
        return false;
    }

    if (index + need > text.size())
    {
        return false;
    }
    for (std::size_t extra = 1; extra < need; ++extra)
    {
        const auto next = static_cast<unsigned char>(text[index + extra]);
        if ((next & 0xC0U) != 0x80U)
        {
            return false;
        }
        code = (code << 6U) | (next & 0x3FU);
    }
    if (code < minimum || code > 0x10FFFFU || (code >= 0xD800U && code <= 0xDFFFU))
    {
        return false;
    }
    out.append(text.data() + index, need);
    index += need;
    return true;
}

static void appendJsonString(std::string &out, std::string_view text)
{
    out.push_back('"');
    std::size_t index = 0;
    while (index < text.size())
    {
        const auto byte = static_cast<unsigned char>(text[index]);
        if (byte < 0x20U || byte == '"' || byte == '\\')
        {
            appendControl(out, byte);
            ++index;
            continue;
        }
        if (byte < 0x80U)
        {
            out.push_back(static_cast<char>(byte));
            ++index;
            continue;
        }
        if (!appendScalar(out, text, index))
        {
            out.append("\uFFFD");
            ++index;
        }
    }
    out.push_back('"');
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
    appendJsonString(out, match.source);
    out.append(",\"kind\":");
    appendJsonString(out, match.arrayElement ? "element" : "line");
    out.append(",\"line\":");
    out.append(std::format("{}", match.line));
    out.append(",\"score\":");
    out.append(std::format("{}", match.score));
    out.append(",\"text\":");
    appendJsonString(out, match.text);
    out.append(",\"occurrences\":");
    out.append(std::format("{}", match.occurrences));
    out.push_back('}');
    return out;
}

} // namespace nesso::cli

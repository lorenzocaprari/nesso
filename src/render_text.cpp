// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <render_text.hpp>

#include <cstddef>
#include <cstdint>

namespace nesso::cli
{

static constexpr std::string_view HEX_DIGITS = "0123456789ABCDEF";

static void appendHexByte(std::string &output, unsigned char byte)
{
    output.append("\\x");
    output.push_back(HEX_DIGITS.at(byte >> 4U));
    output.push_back(HEX_DIGITS.at(byte & 0x0FU));
}

static std::size_t utf8SequenceLength(unsigned char lead)
{
    if (lead >= 0xC2U && lead <= 0xDFU)
    {
        return 2;
    }
    if (lead >= 0xE0U && lead <= 0xEFU)
    {
        return 3;
    }
    if (lead >= 0xF0U && lead <= 0xF4U)
    {
        return 4;
    }
    return 0;
}

static bool decodeUtf8(std::string_view text, std::size_t offset, std::size_t length, std::uint32_t &codepoint)
{
    const auto byte = [&text](std::size_t index) { return static_cast<unsigned char>(text[index]); };
    const unsigned char lead = byte(offset);
    if (length == 2)
    {
        codepoint = lead & 0x1FU;
    }
    else if (length == 3)
    {
        codepoint = lead & 0x0FU;
    }
    else
    {
        codepoint = lead & 0x07U;
    }
    for (std::size_t index = 1; index < length; ++index)
    {
        const unsigned char continuation = byte(offset + index);
        if ((continuation & 0xC0U) != 0x80U)
        {
            return false;
        }
        codepoint = (codepoint << 6U) | (continuation & 0x3FU);
    }
    return (length != 2 || codepoint >= 0x80U) && (length != 3 || codepoint >= 0x800U) &&
           (length != 4 || codepoint >= 0x10000U) && codepoint <= 0x10FFFFU &&
           (codepoint < 0xD800U || codepoint > 0xDFFFU);
}

std::string escapeTerminalControls(std::string_view text)
{
    std::string output;
    output.reserve(text.size());
    for (std::size_t index = 0; index < text.size();)
    {
        const auto current = static_cast<unsigned char>(text[index]);
        if (current < 0x20U || current == 0x7FU)
        {
            appendHexByte(output, current);
            ++index;
            continue;
        }
        if (current < 0x80U)
        {
            output.push_back(static_cast<char>(current));
            ++index;
            continue;
        }

        const std::size_t length = utf8SequenceLength(current);
        std::uint32_t codepoint = 0;
        if (length == 0 || length > text.size() - index || !decodeUtf8(text, index, length, codepoint))
        {
            appendHexByte(output, current);
            ++index;
            continue;
        }
        if (codepoint >= 0x80U && codepoint <= 0x9FU)
        {
            output.append("\\u00");
            output.push_back(HEX_DIGITS.at((codepoint >> 4U) & 0x0FU));
            output.push_back(HEX_DIGITS.at(codepoint & 0x0FU));
        }
        else
        {
            output.append(text.substr(index, length));
        }
        index += length;
    }
    return output;
}

} // namespace nesso::cli

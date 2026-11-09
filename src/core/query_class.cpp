// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <core/query_class.hpp>

#include <algorithm>
#include <array>
#include <cstdint>

namespace nesso::core
{

static constexpr std::array<std::size_t, 5> UUID_GROUP_LENGTHS{8U, 4U, 4U, 4U, 12U};
static constexpr std::array<char, 4> UUID_SEPS{'-', '-', '-', '-'};

static bool isAsciiWhitespace(unsigned char byte) noexcept
{
    return byte == 0x09U || byte == 0x0AU || byte == 0x0DU || byte == 0x20U;
}

static bool isAsciiHex(unsigned char byte) noexcept
{
    return (byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'f') || (byte >= 'A' && byte <= 'F');
}

static bool isAsciiDigit(unsigned char byte) noexcept { return byte >= '0' && byte <= '9'; }

static bool isAsciiLetter(unsigned char byte) noexcept
{
    return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z');
}

static bool stripOneSurroundingQuotes(std::string_view &text) noexcept
{
    if (text.size() < 2U || text.front() != '"' || text.back() != '"')
    {
        return false;
    }
    text.remove_prefix(1);
    text.remove_suffix(1);
    return true;
}

static bool matchUuid(std::string_view token) noexcept
{
    if (token.size() != 36U)
    {
        return false;
    }

    std::size_t index = 0;
    for (std::size_t group = 0; group < 5U; ++group)
    {
        for (std::size_t digit = 0; digit < UUID_GROUP_LENGTHS.at(group); ++digit)
        {
            if (!isAsciiHex(static_cast<unsigned char>(token[index++])))
            {
                return false;
            }
        }
        if (group < 4U)
        {
            if (token[index++] != UUID_SEPS.at(group))
            {
                return false;
            }
        }
    }
    return index == token.size();
}

static bool matchLongHex(std::string_view token) noexcept
{
    std::string_view body = token;
    if (body.size() >= 2U && body[0] == '0' && (body[1] == 'x' || body[1] == 'X'))
    {
        body.remove_prefix(2);
    }
    if (body.size() < 8U)
    {
        return false;
    }

    if (!std::ranges::all_of(body, [](const char ch) { return isAsciiHex(static_cast<unsigned char>(ch)); }))
    {
        return false;
    }
    return std::ranges::any_of(body, [](const char ch) { return isAsciiDigit(static_cast<unsigned char>(ch)); });
}

static bool matchDecimalDigits(std::string_view token) noexcept
{
    if (token.size() < 4U)
    {
        return false;
    }
    return std::ranges::all_of(token, [](const char ch) { return isAsciiDigit(static_cast<unsigned char>(ch)); });
}

static bool matchPrefixedCode(std::string_view token) noexcept
{
    std::size_t index = 0;
    const std::size_t size = token.size();

    std::size_t letters = 0;
    while (index < size && isAsciiLetter(static_cast<unsigned char>(token[index])) && letters < 10U)
    {
        ++letters;
        ++index;
    }
    if (letters < 2U)
    {
        return false;
    }

    if (index >= size || (token[index] != '-' && token[index] != '_'))
    {
        return false;
    }
    ++index;

    if (index < size && token[index] == '-')
    {
        ++index;
    }

    if (index < size && isAsciiLetter(static_cast<unsigned char>(token[index])))
    {
        ++index;
    }

    std::size_t digits = 0;
    while (index < size)
    {
        if (!isAsciiDigit(static_cast<unsigned char>(token[index])))
        {
            return false;
        }
        ++digits;
        ++index;
    }
    return digits >= 3U;
}

static bool parseDecimalPort(std::string_view text, std::uint32_t &port) noexcept
{
    if (text.empty())
    {
        return false;
    }
    std::uint32_t value = 0;
    for (const char ch : text)
    {
        if (!isAsciiDigit(static_cast<unsigned char>(ch)))
        {
            return false;
        }
        value = (value * 10U) + static_cast<std::uint32_t>(ch - '0');
        if (value > 65535U)
        {
            return false;
        }
    }
    port = value;
    return true;
}

static bool parseIpv4Dotted(std::string_view text) noexcept
{
    if (text.empty())
    {
        return false;
    }

    std::size_t index = 0;
    const std::size_t size = text.size();
    int octets = 0;
    while (index < size)
    {
        if (octets == 4)
        {
            return false;
        }
        if (text[index] == '+')
        {
            return false;
        }
        if (!isAsciiDigit(static_cast<unsigned char>(text[index])))
        {
            return false;
        }

        const std::size_t begin = index;
        while (index < size && isAsciiDigit(static_cast<unsigned char>(text[index])))
        {
            ++index;
        }
        const std::size_t length = index - begin;
        if (length == 0U)
        {
            return false;
        }

        std::uint32_t value = 0;
        for (std::size_t digit = begin; digit < index; ++digit)
        {
            value = (value * 10U) + static_cast<std::uint32_t>(text[digit] - '0');
            if (value > 255U)
            {
                return false;
            }
        }

        ++octets;
        if (index >= size)
        {
            break;
        }
        if (text[index] != '.')
        {
            return false;
        }
        ++index;
    }
    return octets == 4;
}

static bool matchIpv4(std::string_view token) noexcept
{
    if (token.empty() || token.front() == '+')
    {
        return false;
    }

    const std::size_t colon = token.find(':');
    const std::string_view host = colon == std::string_view::npos ? token : token.substr(0, colon);
    if (!parseIpv4Dotted(host))
    {
        return false;
    }

    if (colon == std::string_view::npos)
    {
        return true;
    }

    std::uint32_t port = 0;
    return parseDecimalPort(token.substr(colon + 1), port);
}

static bool parseIpv6HexGroup(std::string_view group) noexcept
{
    if (group.empty() || group.size() > 4U)
    {
        return false;
    }
    return std::ranges::all_of(group, [](const char ch) { return isAsciiHex(static_cast<unsigned char>(ch)); });
}

static int countIpv6Pieces(std::string_view text, bool allowIpv4Tail) noexcept
{
    if (text.empty())
    {
        return 0;
    }

    int groups = 0;
    std::size_t index = 0;
    const std::size_t size = text.size();
    while (index < size)
    {
        const std::size_t colon = text.find(':', index);
        const std::string_view piece =
            colon == std::string_view::npos ? text.substr(index) : text.substr(index, colon - index);

        if (allowIpv4Tail && colon == std::string_view::npos && piece.find('.') != std::string_view::npos)
        {
            if (!parseIpv4Dotted(piece))
            {
                return -1;
            }
            groups += 2;
            return groups;
        }

        if (!parseIpv6HexGroup(piece))
        {
            return -1;
        }
        ++groups;

        if (colon == std::string_view::npos)
        {
            break;
        }
        index = colon + 1;
    }
    return groups;
}

static bool parseIpv6Address(std::string_view addr) noexcept
{
    if (addr.empty() || addr.find('%') != std::string_view::npos)
    {
        return false;
    }
    if (addr.find(":::") != std::string_view::npos)
    {
        return false;
    }

    const std::size_t compression = addr.find("::");
    const bool hasCompression = compression != std::string_view::npos;
    if (hasCompression && addr.find("::", compression + 2) != std::string_view::npos)
    {
        return false;
    }

    const std::string_view left = hasCompression ? addr.substr(0, compression) : addr;
    const std::string_view right = hasCompression ? addr.substr(compression + 2) : std::string_view{};

    const int leftGroups = countIpv6Pieces(left, false);
    if (leftGroups < 0)
    {
        return false;
    }
    const int rightGroups = countIpv6Pieces(right, true);
    if (rightGroups < 0)
    {
        return false;
    }

    const int explicitGroups = leftGroups + rightGroups;
    if (explicitGroups > 8)
    {
        return false;
    }

    if (hasCompression)
    {
        return explicitGroups < 8;
    }
    return explicitGroups == 8;
}

static bool matchIpv6(std::string_view token) noexcept
{
    if (token.starts_with('['))
    {
        const std::size_t close = token.find(']');
        if (close == std::string_view::npos || close == 0)
        {
            return false;
        }

        const std::string_view addr = token.substr(1, close - 1);
        if (!parseIpv6Address(addr))
        {
            return false;
        }

        if (close + 1 == token.size())
        {
            return true;
        }
        if (token[close + 1] != ':')
        {
            return false;
        }

        std::uint32_t port = 0;
        return parseDecimalPort(token.substr(close + 2), port);
    }

    if (token.find(':') == std::string_view::npos)
    {
        return false;
    }

    if (parseIpv6Address(token))
    {
        return true;
    }

    const std::size_t lastColon = token.rfind(':');
    const std::string_view tail = token.substr(lastColon + 1);
    if (tail.empty())
    {
        return false;
    }

    if (!std::ranges::all_of(tail, [](const char ch) { return isAsciiDigit(static_cast<unsigned char>(ch)); }))
    {
        return false;
    }

    std::uint32_t port = 0;
    const std::string_view head = token.substr(0, lastColon);
    if (parseDecimalPort(tail, port) && parseIpv6Address(head))
    {
        return false;
    }
    return false;
}

static bool matchUnixAbsolutePath(std::string_view token) noexcept
{
    if (!token.starts_with('/'))
    {
        return false;
    }

    std::size_t index = 1;
    const std::size_t size = token.size();
    std::size_t segments = 0;
    while (index < size)
    {
        while (index < size && token[index] == '/')
        {
            ++index;
        }
        if (index >= size)
        {
            break;
        }

        while (index < size && token[index] != '/')
        {
            ++index;
        }
        ++segments;
    }
    return segments >= 2U;
}

QueryTokens tokenizeQuery(std::string_view query)
{
    QueryTokens result;
    std::string_view strippedView = query;
    stripOneSurroundingQuotes(strippedView);
    result.stripped.assign(strippedView);

    std::size_t index = 0;
    const std::size_t size = strippedView.size();
    while (index < size)
    {
        while (index < size && isAsciiWhitespace(static_cast<unsigned char>(strippedView[index])))
        {
            ++index;
        }
        if (index >= size)
        {
            break;
        }

        const std::size_t begin = index;
        while (index < size && !isAsciiWhitespace(static_cast<unsigned char>(strippedView[index])))
        {
            ++index;
        }

        std::string_view token = strippedView.substr(begin, index - begin);
        stripOneSurroundingQuotes(token);
        if (!token.empty())
        {
            result.tokens.emplace_back(token);
        }
    }
    return result;
}

bool isIdentifierToken(std::string_view token)
{
    return matchUuid(token) || matchLongHex(token) || matchDecimalDigits(token) || matchPrefixedCode(token) ||
           matchIpv4(token) || matchIpv6(token) || matchUnixAbsolutePath(token);
}

QueryClass classifyQuery(std::string_view query)
{
    const QueryTokens tokens = tokenizeQuery(query);
    if (tokens.tokens.empty())
    {
        return QueryClass::NaturalLanguage;
    }

    bool anyIdentifier = false;
    for (const std::string &token : tokens.tokens)
    {
        if (isIdentifierToken(token))
        {
            anyIdentifier = true;
            break;
        }
    }

    if (!anyIdentifier)
    {
        return QueryClass::NaturalLanguage;
    }

    if (tokens.tokens.size() == 1U)
    {
        return QueryClass::Identifier;
    }
    return QueryClass::Mixed;
}

} // namespace nesso::core

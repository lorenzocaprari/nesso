// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <parser/template_mask.hpp>

#include <array>
#include <cstddef>

namespace nesso::parser
{

static bool isDigit(char character) noexcept { return character >= '0' && character <= '9'; }

static bool isHex(char character) noexcept
{
    return isDigit(character) || (character >= 'a' && character <= 'f') || (character >= 'A' && character <= 'F');
}

static bool isLetter(char character) noexcept
{
    return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z');
}

static bool isWord(char character) noexcept { return isLetter(character) || isDigit(character) || character == '_'; }

static bool isPathChar(char character) noexcept
{
    return isWord(character) || character == '/' || character == '.' || character == '-' || character == '+' ||
           character == '~' || character == '%';
}

static int decimalValue(char character) noexcept
{
    return static_cast<unsigned char>(character) - static_cast<unsigned char>('0');
}

static bool readDigits(std::string_view text, std::size_t index, std::size_t count, int &value) noexcept
{
    if (index + count > text.size())
    {
        return false;
    }
    int parsed = 0;
    for (std::size_t offset = 0; offset < count; ++offset)
    {
        const char character = text[index + offset];
        if (!isDigit(character))
        {
            return false;
        }
        parsed = (parsed * 10) + decimalValue(character);
    }
    value = parsed;
    return true;
}

static bool readOctet(std::string_view text, std::size_t index, std::size_t &end) noexcept
{
    if (index >= text.size() || !isDigit(text[index]))
    {
        return false;
    }
    int value = 0;
    std::size_t cursor = index;
    int digits = 0;
    while (cursor < text.size() && isDigit(text[cursor]) && digits < 3)
    {
        value = (value * 10) + decimalValue(text[cursor]);
        ++cursor;
        ++digits;
    }
    if (cursor < text.size() && isDigit(text[cursor]))
    {
        return false;
    }
    if (value > 255)
    {
        return false;
    }
    end = cursor;
    return true;
}

static std::size_t matchPort(std::string_view text, std::size_t index) noexcept
{
    if (index >= text.size() || !isDigit(text[index]))
    {
        return 0;
    }
    int value = 0;
    std::size_t cursor = index;
    int digits = 0;
    while (cursor < text.size() && isDigit(text[cursor]) && digits < 5)
    {
        value = (value * 10) + decimalValue(text[cursor]);
        ++cursor;
        ++digits;
    }
    if (cursor < text.size() && isDigit(text[cursor]))
    {
        return 0;
    }
    if (value > 65535)
    {
        return 0;
    }
    return cursor - index;
}

static std::size_t matchIpv4At(std::string_view text, std::size_t begin, bool checkLead, bool allowPort) noexcept
{
    const std::size_t size = text.size();
    if (checkLead && begin > 0 && (isDigit(text[begin - 1]) || text[begin - 1] == '.'))
    {
        return 0;
    }
    std::size_t cursor = begin;
    for (int part = 0; part < 4; ++part)
    {
        if (part > 0)
        {
            if (cursor >= size || text[cursor] != '.')
            {
                return 0;
            }
            ++cursor;
        }
        std::size_t end = cursor;
        if (!readOctet(text, cursor, end))
        {
            return 0;
        }
        cursor = end;
    }
    if (cursor < size && text[cursor] == '.')
    {
        return 0;
    }
    if (allowPort && cursor < size && text[cursor] == ':')
    {
        const std::size_t port = matchPort(text, cursor + 1);
        if (port == 0)
        {
            return 0;
        }
        cursor += 1 + port;
    }
    if (cursor < size && (isWord(text[cursor]) || text[cursor] == '.'))
    {
        return 0;
    }
    return cursor - begin;
}

static std::size_t matchIpv6(std::string_view text, std::size_t begin) noexcept
{
    const std::size_t size = text.size();
    std::size_t cursor = begin;
    const bool bracket = cursor < size && text[cursor] == '[';
    if (bracket)
    {
        ++cursor;
    }
    else if (begin > 0 && isWord(text[begin - 1]))
    {
        return 0;
    }

    const std::size_t scanEnd = cursor + 64 < size ? cursor + 64 : size;
    int groups = 0;
    bool compressed = false;
    bool sawHex = false;

    while (cursor < scanEnd)
    {
        if (text[cursor] == ':')
        {
            if (compressed || cursor + 1 >= scanEnd || text[cursor + 1] != ':')
            {
                return 0;
            }
            compressed = true;
            cursor += 2;
            continue;
        }

        bool dotted = false;
        if (isDigit(text[cursor]))
        {
            const std::size_t lookEnd = cursor + 16 < scanEnd ? cursor + 16 : scanEnd;
            for (std::size_t look = cursor; look < lookEnd; ++look)
            {
                if (text[look] == '.')
                {
                    dotted = true;
                    break;
                }
                if (!isDigit(text[look]))
                {
                    break;
                }
            }
        }
        if (dotted)
        {
            const std::size_t tail = matchIpv4At(text, cursor, false, false);
            if (tail == 0 || groups > 6)
            {
                return 0;
            }
            groups += 2;
            sawHex = true;
            cursor += tail;
            break;
        }

        if (!isHex(text[cursor]))
        {
            break;
        }
        std::size_t hexes = 0;
        while (hexes < 4 && cursor < scanEnd && isHex(text[cursor]))
        {
            ++cursor;
            ++hexes;
        }
        if (hexes == 0 || (cursor < scanEnd && isHex(text[cursor])))
        {
            return 0;
        }
        sawHex = true;
        ++groups;
        if (groups > 8)
        {
            return 0;
        }
        if (cursor < scanEnd && text[cursor] == ':')
        {
            if (cursor + 1 < scanEnd && text[cursor + 1] == ':')
            {
                continue;
            }
            if (groups == 8)
            {
                return 0;
            }
            ++cursor;
            if (cursor >= scanEnd)
            {
                return 0;
            }
            continue;
        }
        break;
    }

    if (!sawHex || (!compressed && groups != 8) || (compressed && groups >= 8))
    {
        return 0;
    }

    if (bracket)
    {
        if (cursor >= size || text[cursor] != ']')
        {
            return 0;
        }
        ++cursor;
        if (cursor < size && text[cursor] == ':')
        {
            const std::size_t port = matchPort(text, cursor + 1);
            if (port == 0)
            {
                return 0;
            }
            cursor += 1 + port;
        }
    }
    else if (cursor < size)
    {
        const char next = text[cursor];
        if (isWord(next) || next == ':' || next == '.')
        {
            return 0;
        }
    }
    return cursor > begin ? cursor - begin : 0;
}

static std::size_t matchUuid(std::string_view text, std::size_t begin) noexcept
{
    if (begin > 0 && isWord(text[begin - 1]))
    {
        return 0;
    }
    if (begin >= text.size() || !isHex(text[begin]))
    {
        return 0;
    }
    static constexpr std::array<int, 5> widths = {8, 4, 4, 4, 12};
    std::size_t cursor = begin;
    for (std::size_t group = 0; group < widths.size(); ++group)
    {
        if (group > 0)
        {
            if (cursor >= text.size() || text[cursor] != '-')
            {
                return 0;
            }
            ++cursor;
        }
        for (int digit = 0; digit < widths.at(group); ++digit)
        {
            if (cursor >= text.size() || !isHex(text[cursor]))
            {
                return 0;
            }
            ++cursor;
        }
    }
    if (cursor < text.size() && (isHex(text[cursor]) || text[cursor] == '-'))
    {
        return 0;
    }
    return cursor - begin;
}

static std::size_t matchBlk(std::string_view text, std::size_t begin) noexcept
{
    // A digit may end the previous id (blk_1blk_2). A letter or '_' glues a prefix (myblk_1).
    if (begin > 0 && (isLetter(text[begin - 1]) || text[begin - 1] == '_'))
    {
        return 0;
    }
    if (begin + 4 > text.size() || text[begin] != 'b' || text[begin + 1] != 'l' || text[begin + 2] != 'k' ||
        text[begin + 3] != '_')
    {
        return 0;
    }
    std::size_t cursor = begin + 4;
    if (cursor < text.size() && text[cursor] == '-')
    {
        ++cursor;
    }
    if (cursor >= text.size() || !isDigit(text[cursor]))
    {
        return 0;
    }
    while (cursor < text.size() && isDigit(text[cursor]))
    {
        ++cursor;
    }
    if (cursor < text.size() && isWord(text[cursor]))
    {
        // blk_1blk_2 is two ids. A letter suffix such as blk_1abc is not an id.
        const bool nextId = cursor + 4 <= text.size() && text[cursor] == 'b' && text[cursor + 1] == 'l' &&
                            text[cursor + 2] == 'k' && text[cursor + 3] == '_';
        if (!nextId)
        {
            return 0;
        }
    }
    return cursor - begin;
}

static std::size_t matchHms(std::string_view text, std::size_t begin, bool leadBoundary) noexcept
{
    if (leadBoundary && begin > 0 && isDigit(text[begin - 1]))
    {
        return 0;
    }
    int hour = 0;
    int minute = 0;
    int second = 0;
    if (!readDigits(text, begin, 2, hour) || hour > 23 || begin + 2 >= text.size() || text[begin + 2] != ':')
    {
        return 0;
    }
    if (!readDigits(text, begin + 3, 2, minute) || minute > 59 || begin + 5 >= text.size() || text[begin + 5] != ':')
    {
        return 0;
    }
    if (!readDigits(text, begin + 6, 2, second) || second > 60)
    {
        return 0;
    }
    std::size_t cursor = begin + 8;
    if (cursor < text.size() && text[cursor] == '.')
    {
        std::size_t fraction = cursor + 1;
        int digits = 0;
        while (digits < 9 && fraction < text.size() && isDigit(text[fraction]))
        {
            ++fraction;
            ++digits;
        }
        if (digits > 0 && (fraction >= text.size() || !isDigit(text[fraction])))
        {
            cursor = fraction;
        }
    }
    if (cursor < text.size() && isDigit(text[cursor]))
    {
        return 0;
    }
    return cursor - begin;
}

static std::size_t matchZone(std::string_view text, std::size_t begin) noexcept
{
    if (begin >= text.size())
    {
        return 0;
    }
    const char sign = text[begin];
    if (sign == 'Z' || sign == 'z')
    {
        if (begin + 1 < text.size() && isWord(text[begin + 1]))
        {
            return 0;
        }
        return 1;
    }
    if (sign != '+' && sign != '-')
    {
        return 0;
    }
    int hour = 0;
    int minute = 0;
    if (!readDigits(text, begin + 1, 2, hour) || hour > 23)
    {
        return 0;
    }
    std::size_t cursor = begin + 3;
    if (cursor < text.size() && text[cursor] == ':')
    {
        ++cursor;
    }
    if (!readDigits(text, cursor, 2, minute) || minute > 59)
    {
        return 0;
    }
    cursor += 2;
    if (cursor < text.size() && isDigit(text[cursor]))
    {
        return 0;
    }
    return cursor - begin;
}

static std::size_t matchIso(std::string_view text, std::size_t begin) noexcept
{
    if (begin > 0 && isWord(text[begin - 1]))
    {
        return 0;
    }
    if (begin >= text.size() || !isDigit(text[begin]))
    {
        return 0;
    }
    int month = 0;
    int day = 0;
    int year = 0;
    if (!readDigits(text, begin, 4, year) || begin + 4 >= text.size() || text[begin + 4] != '-')
    {
        return 0;
    }
    if (!readDigits(text, begin + 5, 2, month) || month < 1 || month > 12 || begin + 7 >= text.size() ||
        text[begin + 7] != '-')
    {
        return 0;
    }
    if (!readDigits(text, begin + 8, 2, day) || day < 1 || day > 31)
    {
        return 0;
    }
    static_cast<void>(year);
    std::size_t cursor = begin + 10;
    if (cursor < text.size() && (text[cursor] == 'T' || text[cursor] == 't' || text[cursor] == ' '))
    {
        const bool required = text[cursor] == 'T' || text[cursor] == 't';
        const std::size_t time = matchHms(text, cursor + 1, false);
        if (time == 0)
        {
            if (required)
            {
                return 0;
            }
        }
        else
        {
            cursor += 1 + time;
            const std::size_t zone = matchZone(text, cursor);
            if (zone > 0)
            {
                cursor += zone;
            }
        }
    }
    if (cursor < text.size() && isWord(text[cursor]))
    {
        return 0;
    }
    return cursor - begin;
}

static std::size_t matchSyslog(std::string_view text, std::size_t begin) noexcept
{
    if (begin > 0 && isLetter(text[begin - 1]))
    {
        return 0;
    }
    if (begin + 3 > text.size() || !isLetter(text[begin]))
    {
        return 0;
    }
    static constexpr std::array<std::string_view, 12> months = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                                                "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    const std::string_view month = text.substr(begin, 3);
    bool known = false;
    for (const std::string_view candidate : months)
    {
        if (month == candidate)
        {
            known = true;
            break;
        }
    }
    if (!known)
    {
        return 0;
    }
    std::size_t cursor = begin + 3;
    if (cursor >= text.size() || text[cursor] != ' ')
    {
        return 0;
    }
    while (cursor < text.size() && text[cursor] == ' ')
    {
        ++cursor;
    }
    if (cursor >= text.size() || !isDigit(text[cursor]))
    {
        return 0;
    }
    int day = decimalValue(text[cursor]);
    ++cursor;
    if (cursor < text.size() && isDigit(text[cursor]))
    {
        day = (day * 10) + decimalValue(text[cursor]);
        ++cursor;
    }
    if (day < 1 || day > 31 || (cursor < text.size() && isDigit(text[cursor])))
    {
        return 0;
    }
    if (cursor >= text.size() || text[cursor] != ' ')
    {
        return 0;
    }
    while (cursor < text.size() && text[cursor] == ' ')
    {
        ++cursor;
    }
    const std::size_t time = matchHms(text, cursor, false);
    if (time == 0)
    {
        return 0;
    }
    return cursor + time - begin;
}

static std::size_t matchHex(std::string_view text, std::size_t begin) noexcept
{
    const std::size_t size = text.size();
    if (begin >= size || !isHex(text[begin]))
    {
        return 0;
    }
    if (text[begin] == '0' && begin + 1 < size && (text[begin + 1] == 'x' || text[begin + 1] == 'X'))
    {
        if (begin > 0 && isWord(text[begin - 1]))
        {
            return 0;
        }
        std::size_t cursor = begin + 2;
        const std::size_t hexStart = cursor;
        while (cursor < size && isHex(text[cursor]))
        {
            ++cursor;
        }
        if (cursor - hexStart >= 6)
        {
            return cursor - begin;
        }
        return 0;
    }
    if (begin > 0 && isHex(text[begin - 1]))
    {
        return 0;
    }
    std::size_t cursor = begin;
    while (cursor < size && isHex(text[cursor]))
    {
        ++cursor;
    }
    if (cursor - begin < 6)
    {
        return 0;
    }
    bool letter = false;
    for (std::size_t index = begin; index < cursor; ++index)
    {
        if (!isDigit(text[index]))
        {
            letter = true;
            break;
        }
    }
    if (!letter)
    {
        return 0;
    }
    return cursor - begin;
}

static std::size_t matchDecimal(std::string_view text, std::size_t begin) noexcept
{
    if (begin >= text.size())
    {
        return 0;
    }
    std::size_t cursor = begin;
    if ((text[cursor] == '+' || text[cursor] == '-') && cursor + 1 < text.size() && isDigit(text[cursor + 1]))
    {
        if (begin > 0 && isWord(text[begin - 1]))
        {
            return 0;
        }
        ++cursor;
    }
    else if (!isDigit(text[cursor]) || (begin > 0 && isWord(text[begin - 1])))
    {
        return 0;
    }
    if (cursor >= text.size() || !isDigit(text[cursor]))
    {
        return 0;
    }
    while (cursor < text.size() && isDigit(text[cursor]))
    {
        ++cursor;
    }
    if (cursor < text.size() && isWord(text[cursor]))
    {
        return 0;
    }
    return cursor - begin;
}

static std::size_t pathSpan(std::string_view text, std::size_t begin, bool &hasDigit) noexcept
{
    hasDigit = false;
    std::size_t length = 0;
    while (begin + length < text.size() && isPathChar(text[begin + length]))
    {
        if (isDigit(text[begin + length]))
        {
            hasDigit = true;
        }
        ++length;
    }
    return length;
}

static void appendMasked(std::string &out) { out.append(MASKED_TOKEN); }

static std::size_t variableLength(std::string_view text, std::size_t index) noexcept
{
    if (const std::size_t taken = matchUuid(text, index); taken > 0)
    {
        return taken;
    }
    if (const std::size_t taken = matchBlk(text, index); taken > 0)
    {
        return taken;
    }
    if (const std::size_t taken = matchIpv6(text, index); taken > 0)
    {
        return taken;
    }
    if (const std::size_t taken = matchIpv4At(text, index, true, true); taken > 0)
    {
        return taken;
    }
    if (const std::size_t taken = matchIso(text, index); taken > 0)
    {
        return taken;
    }
    if (const std::size_t taken = matchSyslog(text, index); taken > 0)
    {
        return taken;
    }
    if (const std::size_t taken = matchHms(text, index, true); taken > 0)
    {
        return taken;
    }
    if (const std::size_t taken = matchHex(text, index); taken > 0)
    {
        return taken;
    }
    return matchDecimal(text, index);
}

std::string maskTemplate(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    std::size_t index = 0;
    while (index < text.size())
    {
        if (text[index] == '/' && (index == 0 || !isPathChar(text[index - 1])))
        {
            bool digit = false;
            const std::size_t length = pathSpan(text, index, digit);
            // A path with no digit is copied in one span so a long literal path is not rescanned.
            if (length > 1)
            {
                if (digit)
                {
                    appendMasked(out);
                }
                else
                {
                    out.append(text.substr(index, length));
                }
                index += length;
                continue;
            }
        }

        if (const std::size_t taken = variableLength(text, index); taken > 0)
        {
            appendMasked(out);
            index += taken;
            continue;
        }
        out.push_back(text[index]);
        ++index;
    }
    return out;
}

} // namespace nesso::parser

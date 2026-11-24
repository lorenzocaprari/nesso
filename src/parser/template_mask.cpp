// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <parser/template_mask.hpp>

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

namespace nesso::parser
{

static auto IsDigit(char character) noexcept -> bool { return character >= '0' && character <= '9'; }

static auto IsHex(char character) noexcept -> bool
{
    return IsDigit(character) || (character >= 'a' && character <= 'f') || (character >= 'A' && character <= 'F');
}

static auto IsLetter(char character) noexcept -> bool
{
    return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z');
}

static auto IsWord(char character) noexcept -> bool
{
    return IsLetter(character) || IsDigit(character) || character == '_';
}

static auto IsPathChar(char character) noexcept -> bool
{
    return IsWord(character) || character == '/' || character == '.' || character == '-' || character == '+' ||
           character == '~' || character == '%';
}

static auto DecimalValue(char character) noexcept -> int
{
    return static_cast<unsigned char>(character) - static_cast<unsigned char>('0');
}

static auto ReadDigits(std::string_view text, std::size_t index, std::size_t count, int &value) noexcept -> bool
{
    if (index + count > text.size())
    {
        return false;
    }
    int parsed = 0;
    for (std::size_t offset = 0; offset < count; ++offset)
    {
        const char character = text[index + offset];
        if (!IsDigit(character))
        {
            return false;
        }
        parsed = (parsed * 10) + DecimalValue(character);
    }
    value = parsed;
    return true;
}

static auto ReadOctet(std::string_view text, std::size_t index, std::size_t &end) noexcept -> bool
{
    if (index >= text.size() || !IsDigit(text[index]))
    {
        return false;
    }
    int value = 0;
    std::size_t cursor = index;
    int digits = 0;
    while (cursor < text.size() && IsDigit(text[cursor]) && digits < 3)
    {
        value = (value * 10) + DecimalValue(text[cursor]);
        ++cursor;
        ++digits;
    }
    if (cursor < text.size() && IsDigit(text[cursor]))
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

static auto MatchPort(std::string_view text, std::size_t index) noexcept -> std::size_t
{
    if (index >= text.size() || !IsDigit(text[index]))
    {
        return 0;
    }
    int value = 0;
    std::size_t cursor = index;
    int digits = 0;
    while (cursor < text.size() && IsDigit(text[cursor]) && digits < 5)
    {
        value = (value * 10) + DecimalValue(text[cursor]);
        ++cursor;
        ++digits;
    }
    if (cursor < text.size() && IsDigit(text[cursor]))
    {
        return 0;
    }
    if (value > 65535)
    {
        return 0;
    }
    return cursor - index;
}

static auto MatchIpv4At(std::string_view text, std::size_t begin, bool check_lead, bool allow_port) noexcept
    -> std::size_t
{
    const std::size_t size = text.size();
    if (check_lead && begin > 0 && (IsDigit(text[begin - 1]) || text[begin - 1] == '.'))
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
        if (!ReadOctet(text, cursor, end))
        {
            return 0;
        }
        cursor = end;
    }
    if (cursor < size && text[cursor] == '.')
    {
        return 0;
    }
    if (allow_port && cursor < size && text[cursor] == ':')
    {
        const std::size_t port = MatchPort(text, cursor + 1);
        if (port == 0)
        {
            return 0;
        }
        cursor += 1 + port;
    }
    if (cursor < size && (IsWord(text[cursor]) || text[cursor] == '.'))
    {
        return 0;
    }
    return cursor - begin;
}

namespace
{
enum class Ipv6Step : unsigned char
{
    Continue,
    Break,
    Fail
};
} // namespace

static auto Ipv6ScanStart(std::string_view text, std::size_t begin, std::size_t &cursor, bool &bracket) noexcept -> bool
{
    const std::size_t size = text.size();
    cursor = begin;
    bracket = cursor < size && text[cursor] == '[';
    if (bracket)
    {
        ++cursor;
        return true;
    }
    if (begin > 0 && IsWord(text[begin - 1]))
    {
        return false;
    }
    return true;
}

static auto Ipv6TakeCompression(std::string_view text, std::size_t scan_end, std::size_t &cursor,
                                bool &compressed) noexcept -> Ipv6Step
{
    if (text[cursor] != ':')
    {
        return Ipv6Step::Continue;
    }
    if (compressed || cursor + 1 >= scan_end || text[cursor + 1] != ':')
    {
        return Ipv6Step::Fail;
    }
    compressed = true;
    cursor += 2;
    return Ipv6Step::Break;
}

static auto Ipv6EmbeddedIpv4Ahead(std::string_view text, std::size_t cursor, std::size_t scan_end) noexcept -> bool
{
    if (!IsDigit(text[cursor]))
    {
        return false;
    }
    const std::size_t look_end = cursor + 16 < scan_end ? cursor + 16 : scan_end;
    for (std::size_t look = cursor; look < look_end; ++look)
    {
        if (text[look] == '.')
        {
            return true;
        }
        if (!IsDigit(text[look]))
        {
            return false;
        }
    }
    return false;
}

static auto Ipv6TakeEmbeddedIpv4(std::string_view text, std::size_t &cursor, int &groups, bool &saw_hex) noexcept
    -> Ipv6Step
{
    const std::size_t tail = MatchIpv4At(text, cursor, false, false);
    if (tail == 0 || groups > 6)
    {
        return Ipv6Step::Fail;
    }
    groups += 2;
    saw_hex = true;
    cursor += tail;
    return Ipv6Step::Break;
}

static auto Ipv6TakeHextet(std::string_view text, std::size_t scan_end, std::size_t &cursor, int &groups,
                           bool &saw_hex) noexcept -> Ipv6Step
{
    if (!IsHex(text[cursor]))
    {
        return Ipv6Step::Break;
    }
    std::size_t hexes = 0;
    while (hexes < 4 && cursor < scan_end && IsHex(text[cursor]))
    {
        ++cursor;
        ++hexes;
    }
    if (hexes == 0 || (cursor < scan_end && IsHex(text[cursor])))
    {
        return Ipv6Step::Fail;
    }
    saw_hex = true;
    ++groups;
    if (groups > 8)
    {
        return Ipv6Step::Fail;
    }
    if (cursor < scan_end && text[cursor] == ':')
    {
        if (cursor + 1 < scan_end && text[cursor + 1] == ':')
        {
            return Ipv6Step::Continue;
        }
        if (groups == 8)
        {
            return Ipv6Step::Fail;
        }
        ++cursor;
        if (cursor >= scan_end)
        {
            return Ipv6Step::Fail;
        }
        return Ipv6Step::Continue;
    }
    return Ipv6Step::Break;
}

static auto Ipv6BodyValid(bool saw_hex, bool compressed, int groups) noexcept -> bool
{
    return saw_hex && ((compressed && groups < 8) || (!compressed && groups == 8));
}

static auto Ipv6Finish(std::string_view text, std::size_t begin, std::size_t cursor, bool bracket) noexcept
    -> std::size_t
{
    const std::size_t size = text.size();
    if (bracket)
    {
        if (cursor >= size || text[cursor] != ']')
        {
            return 0;
        }
        ++cursor;
        if (cursor < size && text[cursor] == ':')
        {
            const std::size_t port = MatchPort(text, cursor + 1);
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
        if (IsWord(next) || next == ':' || next == '.')
        {
            return 0;
        }
    }
    return cursor > begin ? cursor - begin : 0;
}

static auto MatchIpv6(std::string_view text, std::size_t begin) noexcept -> std::size_t
{
    std::size_t cursor = begin;
    bool bracket = false;
    if (!Ipv6ScanStart(text, begin, cursor, bracket))
    {
        return 0;
    }

    const std::size_t size = text.size();
    const std::size_t scan_end = cursor + 64 < size ? cursor + 64 : size;
    int groups = 0;
    bool compressed = false;
    bool saw_hex = false;

    while (cursor < scan_end)
    {
        const Ipv6Step compression = Ipv6TakeCompression(text, scan_end, cursor, compressed);
        if (compression == Ipv6Step::Fail)
        {
            return 0;
        }
        if (compression == Ipv6Step::Break)
        {
            continue;
        }
        if (Ipv6EmbeddedIpv4Ahead(text, cursor, scan_end))
        {
            if (Ipv6TakeEmbeddedIpv4(text, cursor, groups, saw_hex) == Ipv6Step::Fail)
            {
                return 0;
            }
            break;
        }
        const Ipv6Step hextet = Ipv6TakeHextet(text, scan_end, cursor, groups, saw_hex);
        if (hextet == Ipv6Step::Fail)
        {
            return 0;
        }
        if (hextet == Ipv6Step::Break)
        {
            break;
        }
    }

    if (!Ipv6BodyValid(saw_hex, compressed, groups))
    {
        return 0;
    }
    return Ipv6Finish(text, begin, cursor, bracket);
}

static auto MatchUuid(std::string_view text, std::size_t begin) noexcept -> std::size_t
{
    if (begin > 0 && IsWord(text[begin - 1]))
    {
        return 0;
    }
    if (begin >= text.size() || !IsHex(text[begin]))
    {
        return 0;
    }
    static constexpr std::array<int, 5> WIDTHS = {8, 4, 4, 4, 12};
    std::size_t cursor = begin;
    for (std::size_t group = 0; group < WIDTHS.size(); ++group)
    {
        if (group > 0)
        {
            if (cursor >= text.size() || text[cursor] != '-')
            {
                return 0;
            }
            ++cursor;
        }
        for (int digit = 0; digit < WIDTHS.at(group); ++digit)
        {
            if (cursor >= text.size() || !IsHex(text[cursor]))
            {
                return 0;
            }
            ++cursor;
        }
    }
    if (cursor < text.size() && (IsHex(text[cursor]) || text[cursor] == '-'))
    {
        return 0;
    }
    return cursor - begin;
}

static auto MatchBlk(std::string_view text, std::size_t begin) noexcept -> std::size_t
{
    // A digit may end the previous id (blk_1blk_2). A letter or '_' glues a
    // prefix (myblk_1).
    if (begin > 0 && (IsLetter(text[begin - 1]) || text[begin - 1] == '_'))
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
    if (cursor >= text.size() || !IsDigit(text[cursor]))
    {
        return 0;
    }
    while (cursor < text.size() && IsDigit(text[cursor]))
    {
        ++cursor;
    }
    if (cursor < text.size() && IsWord(text[cursor]))
    {
        // blk_1blk_2 is two ids. A letter suffix such as blk_1abc is not an id.
        const bool next_id = cursor + 4 <= text.size() && text[cursor] == 'b' && text[cursor + 1] == 'l' &&
                             text[cursor + 2] == 'k' && text[cursor + 3] == '_';
        if (!next_id)
        {
            return 0;
        }
    }
    return cursor - begin;
}

static auto MatchHms(std::string_view text, std::size_t begin, bool lead_boundary) noexcept -> std::size_t
{
    if (lead_boundary && begin > 0 && IsDigit(text[begin - 1]))
    {
        return 0;
    }
    int hour = 0;
    int minute = 0;
    int second = 0;
    if (!ReadDigits(text, begin, 2, hour) || hour > 23 || begin + 2 >= text.size() || text[begin + 2] != ':')
    {
        return 0;
    }
    if (!ReadDigits(text, begin + 3, 2, minute) || minute > 59 || begin + 5 >= text.size() || text[begin + 5] != ':')
    {
        return 0;
    }
    if (!ReadDigits(text, begin + 6, 2, second) || second > 60)
    {
        return 0;
    }
    std::size_t cursor = begin + 8;
    if (cursor < text.size() && text[cursor] == '.')
    {
        std::size_t fraction = cursor + 1;
        int digits = 0;
        while (digits < 9 && fraction < text.size() && IsDigit(text[fraction]))
        {
            ++fraction;
            ++digits;
        }
        if (digits > 0 && (fraction >= text.size() || !IsDigit(text[fraction])))
        {
            cursor = fraction;
        }
    }
    if (cursor < text.size() && IsDigit(text[cursor]))
    {
        return 0;
    }
    return cursor - begin;
}

static auto MatchZone(std::string_view text, std::size_t begin) noexcept -> std::size_t
{
    if (begin >= text.size())
    {
        return 0;
    }
    const char sign = text[begin];
    if (sign == 'Z' || sign == 'z')
    {
        if (begin + 1 < text.size() && IsWord(text[begin + 1]))
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
    if (!ReadDigits(text, begin + 1, 2, hour) || hour > 23)
    {
        return 0;
    }
    std::size_t cursor = begin + 3;
    if (cursor < text.size() && text[cursor] == ':')
    {
        ++cursor;
    }
    if (!ReadDigits(text, cursor, 2, minute) || minute > 59)
    {
        return 0;
    }
    cursor += 2;
    if (cursor < text.size() && IsDigit(text[cursor]))
    {
        return 0;
    }
    return cursor - begin;
}

static auto MatchIso(std::string_view text, std::size_t begin) noexcept -> std::size_t
{
    if (begin > 0 && IsWord(text[begin - 1]))
    {
        return 0;
    }
    if (begin >= text.size() || !IsDigit(text[begin]))
    {
        return 0;
    }
    int month = 0;
    int day = 0;
    int year = 0;
    if (!ReadDigits(text, begin, 4, year) || begin + 4 >= text.size() || text[begin + 4] != '-')
    {
        return 0;
    }
    if (!ReadDigits(text, begin + 5, 2, month) || month < 1 || month > 12 || begin + 7 >= text.size() ||
        text[begin + 7] != '-')
    {
        return 0;
    }
    if (!ReadDigits(text, begin + 8, 2, day) || day < 1 || day > 31)
    {
        return 0;
    }
    static_cast<void>(year);
    std::size_t cursor = begin + 10;
    if (cursor < text.size() && (text[cursor] == 'T' || text[cursor] == 't' || text[cursor] == ' '))
    {
        const bool required = text[cursor] == 'T' || text[cursor] == 't';
        const std::size_t time = MatchHms(text, cursor + 1, false);
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
            const std::size_t zone = MatchZone(text, cursor);
            if (zone > 0)
            {
                cursor += zone;
            }
        }
    }
    if (cursor < text.size() && IsWord(text[cursor]))
    {
        return 0;
    }
    return cursor - begin;
}

static auto MatchSyslog(std::string_view text, std::size_t begin) noexcept -> std::size_t
{
    if (begin > 0 && IsLetter(text[begin - 1]))
    {
        return 0;
    }
    if (begin + 3 > text.size() || !IsLetter(text[begin]))
    {
        return 0;
    }
    static constexpr std::array<std::string_view, 12> MONTHS = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                                                "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    const std::string_view month = text.substr(begin, 3);
    bool known = false;
    for (const std::string_view candidate : MONTHS)
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
    if (cursor >= text.size() || !IsDigit(text[cursor]))
    {
        return 0;
    }
    int day = DecimalValue(text[cursor]);
    ++cursor;
    if (cursor < text.size() && IsDigit(text[cursor]))
    {
        day = (day * 10) + DecimalValue(text[cursor]);
        ++cursor;
    }
    if (day < 1 || day > 31 || (cursor < text.size() && IsDigit(text[cursor])))
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
    const std::size_t time = MatchHms(text, cursor, false);
    if (time == 0)
    {
        return 0;
    }
    return cursor + time - begin;
}

static auto MatchHex(std::string_view text, std::size_t begin) noexcept -> std::size_t
{
    const std::size_t size = text.size();
    if (begin >= size || !IsHex(text[begin]))
    {
        return 0;
    }
    if (text[begin] == '0' && begin + 1 < size && (text[begin + 1] == 'x' || text[begin + 1] == 'X'))
    {
        if (begin > 0 && IsWord(text[begin - 1]))
        {
            return 0;
        }
        std::size_t cursor = begin + 2;
        const std::size_t hex_start = cursor;
        while (cursor < size && IsHex(text[cursor]))
        {
            ++cursor;
        }
        if (cursor - hex_start >= 6)
        {
            return cursor - begin;
        }
        return 0;
    }
    if (begin > 0 && IsHex(text[begin - 1]))
    {
        return 0;
    }
    std::size_t cursor = begin;
    while (cursor < size && IsHex(text[cursor]))
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
        if (!IsDigit(text[index]))
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

static auto MatchDecimal(std::string_view text, std::size_t begin) noexcept -> std::size_t
{
    if (begin >= text.size())
    {
        return 0;
    }
    std::size_t cursor = begin;
    if ((text[cursor] == '+' || text[cursor] == '-') && cursor + 1 < text.size() && IsDigit(text[cursor + 1]))
    {
        if (begin > 0 && IsWord(text[begin - 1]))
        {
            return 0;
        }
        ++cursor;
    }
    else if (!IsDigit(text[cursor]) || (begin > 0 && IsWord(text[begin - 1])))
    {
        return 0;
    }
    if (cursor >= text.size() || !IsDigit(text[cursor]))
    {
        return 0;
    }
    while (cursor < text.size() && IsDigit(text[cursor]))
    {
        ++cursor;
    }
    if (cursor < text.size() && IsWord(text[cursor]))
    {
        return 0;
    }
    return cursor - begin;
}

static auto PathSpan(std::string_view text, std::size_t begin, bool &has_digit) noexcept -> std::size_t
{
    has_digit = false;
    std::size_t length = 0;
    while (begin + length < text.size() && IsPathChar(text[begin + length]))
    {
        if (IsDigit(text[begin + length]))
        {
            has_digit = true;
        }
        ++length;
    }
    return length;
}

static auto IsJoiner(char character) noexcept -> bool
{
    return character == '.' || character == ':' || character == '@' || character == '/' || character == '-';
}

static auto IsCompoundChar(char character) noexcept -> bool { return IsWord(character) || IsJoiner(character); }

static constexpr std::size_t MIXED_MIN_LENGTH = 3;

// A compound token that mixes letters and digits, such as a host or node name
// (`dn228`, `R02-M1-N0-C:J12-U11`), a size or duration (`126MB`, `30000ms`), or
// a short hex value (`0x01`). It starts at a word byte that does not follow a
// compound byte, runs over word bytes and single joiners, and drops trailing
// joiners.
static auto MatchMixed(std::string_view text, std::size_t begin) noexcept -> std::size_t
{
    if (!IsWord(text[begin]) || (begin != 0 && IsCompoundChar(text[begin - 1])))
    {
        return 0;
    }
    std::size_t end = begin;
    while (end < text.size() && IsCompoundChar(text[end]))
    {
        // Two joiners in a row (a dot leader such as `dcbf.........0`) end
        // the token, so the word before them stays literal.
        if (IsJoiner(text[end]) && end > begin && IsJoiner(text[end - 1]))
        {
            break;
        }
        ++end;
    }
    while (end > begin && IsJoiner(text[end - 1]))
    {
        --end;
    }
    bool letter = false;
    bool digit = false;
    for (std::size_t index = begin; index < end; ++index)
    {
        letter = letter || IsLetter(text[index]);
        digit = digit || IsDigit(text[index]);
    }
    if (end - begin < MIXED_MIN_LENGTH || !letter || !digit)
    {
        return 0;
    }
    return end - begin;
}

static void AppendMasked(std::string &out) { out.append(MASKED_TOKEN); }

static auto VariableLength(std::string_view text, std::size_t index) noexcept -> std::size_t
{
    if (const std::size_t taken = MatchUuid(text, index); taken > 0)
    {
        return taken;
    }
    if (const std::size_t taken = MatchBlk(text, index); taken > 0)
    {
        return taken;
    }
    if (const std::size_t taken = MatchIpv6(text, index); taken > 0)
    {
        return taken;
    }
    if (const std::size_t taken = MatchIpv4At(text, index, true, true); taken > 0)
    {
        return taken;
    }
    if (const std::size_t taken = MatchIso(text, index); taken > 0)
    {
        return taken;
    }
    if (const std::size_t taken = MatchSyslog(text, index); taken > 0)
    {
        return taken;
    }
    if (const std::size_t taken = MatchHms(text, index, true); taken > 0)
    {
        return taken;
    }
    if (const std::size_t taken = MatchHex(text, index); taken > 0)
    {
        return taken;
    }
    if (const std::size_t taken = MatchDecimal(text, index); taken > 0)
    {
        return taken;
    }
    return MatchMixed(text, index);
}

static auto TryMaskPath(std::string_view text, std::size_t &index, std::string &out) -> bool
{
    if (text[index] != '/' || (index != 0 && IsPathChar(text[index - 1])))
    {
        return false;
    }
    bool digit = false;
    const std::size_t length = PathSpan(text, index, digit);
    // A path with no digit is copied in one span so a long literal path is not
    // rescanned.
    if (length <= 1)
    {
        return false;
    }
    if (digit)
    {
        AppendMasked(out);
    }
    else
    {
        out.append(text.substr(index, length));
    }
    index += length;
    return true;
}

auto MaskTemplate(std::string_view text) -> std::string
{
    std::string out;
    out.reserve(text.size());
    std::size_t index = 0;
    while (index < text.size())
    {
        if (TryMaskPath(text, index, out))
        {
            continue;
        }
        if (const std::size_t taken = VariableLength(text, index); taken > 0)
        {
            AppendMasked(out);
            index += taken;
            continue;
        }
        out.push_back(text[index]);
        ++index;
    }
    return out;
}

} // namespace nesso::parser

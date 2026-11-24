// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <parser/line_header.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string_view>
#include <utility>

namespace nesso::parser
{

static constexpr std::size_t NO_MATCH = std::string_view::npos;
static constexpr std::size_t MAX_PREFIX_TOKEN = 128;
static constexpr std::size_t MAX_FIELD_BYTES = 128;
static constexpr std::size_t MAX_TAG_NAME = 96;
static constexpr std::size_t MAX_PIPE_FIELD = 64;

static bool IsDigit(char character) noexcept { return character >= '0' && character <= '9'; }

static bool IsUpper(char character) noexcept { return character >= 'A' && character <= 'Z'; }

static bool IsLetter(char character) noexcept { return IsUpper(character) || (character >= 'a' && character <= 'z'); }

static bool IsBlank(char character) noexcept { return character == ' ' || character == '\t'; }

static char Lower(char character) noexcept
{
    return IsUpper(character) ? static_cast<char>(character - 'A' + 'a') : character;
}

static bool At(std::string_view text, std::size_t index, char character) noexcept
{
    return index < text.size() && text[index] == character;
}

// A token ends at the end of the line or before a blank or a `|`.
static bool IsBoundary(std::string_view text, std::size_t index) noexcept
{
    return index == text.size() || (index < text.size() && (IsBlank(text[index]) || text[index] == '|'));
}

// Exactly `count` digits at `index` whose value is in [low, high].
static bool ReadNumber(std::string_view text, std::size_t index, std::size_t count, int low, int high) noexcept
{
    if (index > text.size() || count > text.size() - index)
    {
        return false;
    }
    int value = 0;
    for (std::size_t offset = 0; offset < count; ++offset)
    {
        const char character = text[index + offset];
        if (!IsDigit(character))
        {
            return false;
        }
        value = (value * 10) + (character - '0');
    }
    return value >= low && value <= high;
}

static std::size_t DigitRun(std::string_view text, std::size_t index) noexcept
{
    std::size_t end = index;
    while (end < text.size() && IsDigit(text[end]))
    {
        ++end;
    }
    return end - index;
}

static bool MatchWord(std::string_view text, std::size_t index, std::string_view word) noexcept
{
    return index <= text.size() && text.substr(index, word.size()) == word;
}

// `Jan` to `Dec`, case-exact.
static bool MatchMonthName(std::string_view text, std::size_t index) noexcept
{
    static constexpr std::array<std::string_view, 12> MONTHS{"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                                             "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    return std::ranges::any_of(MONTHS, [&](std::string_view month) { return MatchWord(text, index, month); });
}

static bool MatchWeekdayName(std::string_view text, std::size_t index) noexcept
{
    static constexpr std::array<std::string_view, 7> DAYS{"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
    return std::ranges::any_of(DAYS, [&](std::string_view day) { return MatchWord(text, index, day); });
}

// `YYYY-MM-DD`.
static std::size_t MatchIsoDate(std::string_view text, std::size_t index) noexcept
{
    if (ReadNumber(text, index, 4, 0, 9999) && At(text, index + 4, '-') && ReadNumber(text, index + 5, 2, 1, 12) &&
        At(text, index + 7, '-') && ReadNumber(text, index + 8, 2, 1, 31))
    {
        return index + 10;
    }
    return NO_MATCH;
}

// `YYYY.MM.DD`.
static std::size_t MatchDottedDate(std::string_view text, std::size_t index) noexcept
{
    if (ReadNumber(text, index, 4, 0, 9999) && At(text, index + 4, '.') && ReadNumber(text, index + 5, 2, 1, 12) &&
        At(text, index + 7, '.') && ReadNumber(text, index + 8, 2, 1, 31))
    {
        return index + 10;
    }
    return NO_MATCH;
}

// `.` or `,` and 1 to 9 digits, or nothing.
static std::size_t SkipFraction(std::string_view text, std::size_t index) noexcept
{
    if (!At(text, index, '.') && !At(text, index, ','))
    {
        return index;
    }
    const std::size_t digits = DigitRun(text, index + 1);
    return digits >= 1 && digits <= 9 ? index + 1 + digits : index;
}

// `HH:MM:SS` with an optional fraction.
static std::size_t MatchClock(std::string_view text, std::size_t index) noexcept
{
    if (ReadNumber(text, index, 2, 0, 23) && At(text, index + 2, ':') && ReadNumber(text, index + 3, 2, 0, 59) &&
        At(text, index + 5, ':') && ReadNumber(text, index + 6, 2, 0, 60))
    {
        return SkipFraction(text, index + 8);
    }
    return NO_MATCH;
}

// `Z`, `+HH:MM`, `+HHMM`, or nothing.
static std::size_t SkipZone(std::string_view text, std::size_t index) noexcept
{
    if (At(text, index, 'Z'))
    {
        return index + 1;
    }
    if (!At(text, index, '+') && !At(text, index, '-'))
    {
        return index;
    }
    if (!ReadNumber(text, index + 1, 2, 0, 23))
    {
        return index;
    }
    const std::size_t minutes = At(text, index + 3, ':') ? index + 4 : index + 3;
    return ReadNumber(text, minutes, 2, 0, 59) ? minutes + 2 : index;
}

// `YYYY-MM-DD` and `T` or a space and a clock, with an optional zone.
static std::size_t MatchIsoTimestamp(std::string_view text, std::size_t index) noexcept
{
    const std::size_t date = MatchIsoDate(text, index);
    if (date == NO_MATCH || !(At(text, date, 'T') || At(text, date, ' ')))
    {
        return NO_MATCH;
    }
    const std::size_t clock = MatchClock(text, date + 1);
    return clock == NO_MATCH ? NO_MATCH : SkipZone(text, clock);
}

// RFC 3164 `Mmm D HH:MM:SS` (one or two blanks before a one-digit day), with
// an optional year.
static std::size_t MatchSyslogTimestamp(std::string_view text, std::size_t index) noexcept
{
    if (!MatchMonthName(text, index) || !At(text, index + 3, ' '))
    {
        return NO_MATCH;
    }
    std::size_t cursor = index + 4;
    if (At(text, cursor, ' '))
    {
        ++cursor;
    }
    const std::size_t digits = DigitRun(text, cursor);
    if (digits < 1 || digits > 2 || !ReadNumber(text, cursor, digits, 1, 31) || !At(text, cursor + digits, ' '))
    {
        return NO_MATCH;
    }
    const std::size_t clock = MatchClock(text, cursor + digits + 1);
    if (clock == NO_MATCH)
    {
        return NO_MATCH;
    }
    if (At(text, clock, ' ') && ReadNumber(text, clock + 1, 4, 0, 9999) && IsBoundary(text, clock + 5))
    {
        return clock + 5;
    }
    return clock;
}

// `Www Mmm DD HH:MM:SS YYYY`.
static std::size_t MatchCtime(std::string_view text, std::size_t index) noexcept
{
    if (!MatchWeekdayName(text, index) || !At(text, index + 3, ' ') || !MatchMonthName(text, index + 4) ||
        !At(text, index + 7, ' ') || !ReadNumber(text, index + 8, 2, 1, 31) || !At(text, index + 10, ' '))
    {
        return NO_MATCH;
    }
    const std::size_t clock = MatchClock(text, index + 11);
    if (clock == NO_MATCH || !At(text, clock, ' ') || !ReadNumber(text, clock + 1, 4, 0, 9999))
    {
        return NO_MATCH;
    }
    return clock + 5;
}

// `DD/Mmm/YYYY:HH:MM:SS +zzzz`.
static std::size_t MatchCommonLogTime(std::string_view text, std::size_t index) noexcept
{
    if (!ReadNumber(text, index, 2, 1, 31) || !At(text, index + 2, '/') || !MatchMonthName(text, index + 3) ||
        !At(text, index + 6, '/') || !ReadNumber(text, index + 7, 4, 0, 9999) || !At(text, index + 11, ':'))
    {
        return NO_MATCH;
    }
    const std::size_t clock = MatchClock(text, index + 12);
    if (clock == NO_MATCH || !At(text, clock, ' '))
    {
        return NO_MATCH;
    }
    const std::size_t zone = SkipZone(text, clock + 1);
    return zone == clock + 1 ? NO_MATCH : zone;
}

// One of the timestamps above inside `[` and `]`.
static std::size_t MatchBracketTimestamp(std::string_view text, std::size_t index) noexcept
{
    if (!At(text, index, '['))
    {
        return NO_MATCH;
    }
    for (const auto match : {MatchIsoTimestamp, MatchCtime, MatchCommonLogTime})
    {
        const std::size_t end = match(text, index + 1);
        if (end != NO_MATCH && At(text, end, ']'))
        {
            return end + 1;
        }
    }
    return NO_MATCH;
}

// A 10- or 13-digit epoch, then blanks and a date. The date is part of the
// match.
static std::size_t MatchEpoch(std::string_view text, std::size_t index) noexcept
{
    const std::size_t digits = DigitRun(text, index);
    if ((digits != 10 && digits != 13) || !IsBlank(index + digits < text.size() ? text[index + digits] : '\0'))
    {
        return NO_MATCH;
    }
    std::size_t date = index + digits;
    while (date < text.size() && IsBlank(text[date]))
    {
        ++date;
    }
    for (const auto match : {MatchSyslogTimestamp, MatchIsoTimestamp, MatchIsoDate, MatchDottedDate})
    {
        if (const std::size_t end = match(text, date); end != NO_MATCH && IsBoundary(text, end))
        {
            return end;
        }
    }
    return NO_MATCH;
}

// 1 or 2 digits at `index` with a value in [low, high]. Returns the end.
static std::size_t ReadShortNumber(std::string_view text, std::size_t index, int high) noexcept
{
    const std::size_t digits = DigitRun(text, index);
    return digits >= 1 && digits <= 2 && ReadNumber(text, index, digits, 0, high) ? index + digits : NO_MATCH;
}

// `H:M:S`, each field 1 or 2 digits.
static std::size_t MatchLooseClock(std::string_view text, std::size_t index) noexcept
{
    const std::size_t hours = ReadShortNumber(text, index, 23);
    if (hours == NO_MATCH || !At(text, hours, ':'))
    {
        return NO_MATCH;
    }
    const std::size_t minutes = ReadShortNumber(text, hours + 1, 59);
    if (minutes == NO_MATCH || !At(text, minutes, ':'))
    {
        return NO_MATCH;
    }
    return ReadShortNumber(text, minutes + 1, 60);
}

// `YYMMDD HHMMSS`, or `YYYYMMDD-H:M:S` (fields of 1 or 2 digits) with optional
// `:fff` or `.fff`.
static std::size_t MatchCompactTimestamp(std::string_view text, std::size_t index) noexcept
{
    if (ReadNumber(text, index, 2, 0, 99) && ReadNumber(text, index + 2, 2, 1, 12) &&
        ReadNumber(text, index + 4, 2, 1, 31) && At(text, index + 6, ' ') && ReadNumber(text, index + 7, 2, 0, 23) &&
        ReadNumber(text, index + 9, 2, 0, 59) && ReadNumber(text, index + 11, 2, 0, 60) && IsBoundary(text, index + 13))
    {
        return index + 13;
    }
    if (!ReadNumber(text, index, 4, 0, 9999) || !ReadNumber(text, index + 4, 2, 1, 12) ||
        !ReadNumber(text, index + 6, 2, 1, 31) || !At(text, index + 8, '-'))
    {
        return NO_MATCH;
    }
    const std::size_t clock = MatchLooseClock(text, index + 9);
    if (clock == NO_MATCH)
    {
        return NO_MATCH;
    }
    const std::size_t millis = DigitRun(text, clock + 1);
    if ((At(text, clock, ':') || At(text, clock, '.')) && millis >= 1 && millis <= 6)
    {
        return clock + 1 + millis;
    }
    return clock;
}

namespace
{
struct Anchor
{
    std::size_t end = NO_MATCH;
    // An RFC 3164 timestamp: the next token is the host.
    bool host_next = false;
};
} // namespace

static Anchor MatchAnchor(std::string_view text, std::size_t index) noexcept
{
    if (const std::size_t end = MatchSyslogTimestamp(text, index); end != NO_MATCH)
    {
        return {.end = end, .host_next = true};
    }
    for (const auto match : {MatchIsoTimestamp, MatchBracketTimestamp, MatchEpoch, MatchCompactTimestamp})
    {
        if (const std::size_t end = match(text, index); end != NO_MATCH)
        {
            return {.end = end, .host_next = false};
        }
    }
    return {};
}

// `<N>`, 1 to 3 digits.
static std::size_t SkipPriority(std::string_view text) noexcept
{
    if (!At(text, 0, '<'))
    {
        return 0;
    }
    const std::size_t digits = DigitRun(text, 1);
    return digits >= 1 && digits <= 3 && At(text, 1 + digits, '>') ? digits + 2 : 0;
}

// `-` or an upper-case word of 2 to 16 bytes, a blank, then an epoch anchor.
// Returns where the epoch starts.
static std::size_t SkipLabel(std::string_view text, std::size_t index) noexcept
{
    std::size_t end = index;
    if (At(text, index, '-'))
    {
        end = index + 1;
    }
    else if (index < text.size() && IsUpper(text[index]))
    {
        while (end < text.size() && (IsUpper(text[end]) || IsDigit(text[end]) || text[end] == '_') && end - index < 16)
        {
            ++end;
        }
        if (end - index < 2)
        {
            return NO_MATCH;
        }
    }
    else
    {
        return NO_MATCH;
    }
    if (!At(text, end, ' ') || MatchEpoch(text, end + 1) == NO_MATCH)
    {
        return NO_MATCH;
    }
    return end + 1;
}

// A token of up to 128 bytes that contains `.log`, blanks, then an ISO 8601
// date and time. Returns where the timestamp starts.
static std::size_t SkipLogFileName(std::string_view text, std::size_t index) noexcept
{
    std::size_t end = index;
    while (end < text.size() && !IsBlank(text[end]) && end - index < MAX_PREFIX_TOKEN)
    {
        ++end;
    }
    if (end == index || end >= text.size() || !IsBlank(text[end]) || !text.substr(index, end - index).contains(".log"))
    {
        return NO_MATCH;
    }
    while (end < text.size() && IsBlank(text[end]))
    {
        ++end;
    }
    return MatchIsoTimestamp(text, end) == NO_MATCH ? NO_MATCH : end;
}

// One RFC 5424 structured-data element list: `-`, or `[...]` blocks that
// honor `\]`.
static std::size_t SkipStructuredData(std::string_view text, std::size_t index) noexcept
{
    if (At(text, index, '-'))
    {
        return index + 1;
    }
    std::size_t cursor = index;
    while (At(text, cursor, '['))
    {
        ++cursor;
        while (cursor < text.size() && text[cursor] != ']')
        {
            cursor += text[cursor] == '\\' ? std::size_t{2} : std::size_t{1};
        }
        if (cursor >= text.size())
        {
            return NO_MATCH;
        }
        ++cursor;
    }
    return cursor == index ? NO_MATCH : cursor;
}

// RFC 5424 after the priority: `1 TIMESTAMP HOST APP PROCID MSGID SD`. The
// header ends after HOST: APP-NAME is a program name and stays in the message.
static std::size_t MatchRfc5424(std::string_view text, std::size_t index) noexcept
{
    if (!At(text, index, '1') || !At(text, index + 1, ' '))
    {
        return NO_MATCH;
    }
    std::size_t cursor = At(text, index + 2, '-') ? index + 3 : MatchIsoTimestamp(text, index + 2);
    if (cursor == NO_MATCH || !At(text, cursor, ' '))
    {
        return NO_MATCH;
    }
    std::size_t host_end = NO_MATCH;
    for (int field = 0; field < 4; ++field)
    {
        const std::size_t start = ++cursor;
        while (cursor < text.size() && text[cursor] != ' ')
        {
            ++cursor;
        }
        if (cursor == start || cursor >= text.size())
        {
            return NO_MATCH;
        }
        if (field == 0)
        {
            host_end = cursor;
        }
    }
    return SkipStructuredData(text, cursor + 1) == NO_MATCH ? NO_MATCH : host_end;
}

// Blanks, then an optional `|` or a `-` between blanks, then blanks.
static std::size_t SkipSeparator(std::string_view text, std::size_t index, bool &pipe) noexcept
{
    std::size_t cursor = index;
    while (cursor < text.size() && IsBlank(text[cursor]))
    {
        ++cursor;
    }
    if (At(text, cursor, '|'))
    {
        pipe = true;
        ++cursor;
    }
    else if (cursor > index && At(text, cursor, '-') && cursor + 1 < text.size() && IsBlank(text[cursor + 1]))
    {
        ++cursor;
    }
    while (cursor < text.size() && IsBlank(text[cursor]))
    {
        ++cursor;
    }
    return cursor;
}

static bool IsLevelWord(std::string_view word) noexcept
{
    static constexpr std::array<std::string_view, 17> LEVELS{
        "trace",  "debug", "dbg",      "info",  "notice", "warn",  "warning", "error",  "err",
        "severe", "crit",  "critical", "fatal", "alert",  "emerg", "panic",   "verbose"};
    if (word.empty() || word.size() > 8)
    {
        return false;
    }
    std::array<char, 8> lower{};
    std::ranges::transform(word, lower.begin(), Lower);
    const std::string_view folded{lower.data(), word.size()};
    return std::ranges::find(LEVELS, folded) != LEVELS.end();
}

// A level: bare, `[x]`, `<x>`, or `x:`.
static std::size_t MatchLevel(std::string_view text, std::size_t index) noexcept
{
    char close = '\0';
    std::size_t start = index;
    if (At(text, index, '['))
    {
        close = ']';
        ++start;
    }
    else if (At(text, index, '<'))
    {
        close = '>';
        ++start;
    }
    std::size_t end = start;
    while (end < text.size() && IsLetter(text[end]))
    {
        ++end;
    }
    if (!IsLevelWord(text.substr(start, end - start)))
    {
        return NO_MATCH;
    }
    if (close != '\0')
    {
        return At(text, end, close) && IsBoundary(text, end + 1) ? end + 1 : NO_MATCH;
    }
    if (At(text, end, ':') && IsBoundary(text, end + 1))
    {
        return end + 1;
    }
    return IsBoundary(text, end) ? end : NO_MATCH;
}

static bool IsTagNameChar(char character) noexcept
{
    return IsLetter(character) || IsDigit(character) || character == '_' || character == '.' || character == '$' ||
           character == '/' || character == '@' || character == '-';
}

// `name`, an optional `(sub)`, an optional `[pid]`, and `:` before a blank.
static std::size_t MatchTag(std::string_view text, std::size_t index) noexcept
{
    if (index >= text.size() || (!IsLetter(text[index]) && text[index] != '_'))
    {
        return NO_MATCH;
    }
    std::size_t cursor = index + 1;
    while (cursor < text.size() && IsTagNameChar(text[cursor]) && cursor - index < MAX_TAG_NAME)
    {
        ++cursor;
    }
    if (At(text, cursor, '('))
    {
        const std::size_t close = text.find(')', cursor);
        if (close == std::string_view::npos || close - cursor > MAX_FIELD_BYTES / 2 ||
            std::ranges::any_of(text.substr(cursor, close - cursor), IsBlank))
        {
            return NO_MATCH;
        }
        cursor = close + 1;
    }
    if (At(text, cursor, '['))
    {
        const std::size_t digits = DigitRun(text, cursor + 1);
        if (digits < 1 || digits > 10 || !At(text, cursor + 1 + digits, ']'))
        {
            return NO_MATCH;
        }
        cursor += digits + 2;
    }
    return At(text, cursor, ':') && (cursor + 1 == text.size() || IsBlank(text[cursor + 1])) ? cursor + 1 : NO_MATCH;
}

// `[...]`, up to 128 bytes, with no `[` inside.
static std::size_t MatchBracketField(std::string_view text, std::size_t index) noexcept
{
    if (!At(text, index, '['))
    {
        return NO_MATCH;
    }
    const std::size_t close = text.find(']', index + 1);
    if (close == std::string_view::npos || close - index >= MAX_FIELD_BYTES ||
        text.substr(index + 1, close - index - 1).contains('[') || !IsBoundary(text, close + 1))
    {
        return NO_MATCH;
    }
    return close + 1;
}

// A dotted name such as `app.db` or `org.apache.Main$Inner`, with no dot
// leader (`..`).
static std::size_t MatchComponent(std::string_view text, std::size_t index) noexcept
{
    std::size_t cursor = index;
    bool dot = false;
    bool letter = false;
    while (cursor < text.size() && cursor - index < MAX_FIELD_BYTES &&
           (IsLetter(text[cursor]) || IsDigit(text[cursor]) || text[cursor] == '_' || text[cursor] == '.' ||
            text[cursor] == '$'))
    {
        dot = dot || text[cursor] == '.';
        letter = letter || IsLetter(text[cursor]);
        ++cursor;
    }
    if (!dot || !letter || text[index] == '.' || text[cursor - 1] == '.' || !IsBoundary(text, cursor) ||
        text.substr(index, cursor - index).contains(".."))
    {
        return NO_MATCH;
    }
    return cursor;
}

// `YYYY-MM-DD-HH.MM.SS` with an optional fraction.
static std::size_t MatchDashedTimestamp(std::string_view text, std::size_t index) noexcept
{
    const std::size_t date = MatchIsoDate(text, index);
    if (date == NO_MATCH || !At(text, date, '-') || !ReadNumber(text, date + 1, 2, 0, 23) || !At(text, date + 3, '.') ||
        !ReadNumber(text, date + 4, 2, 0, 59) || !At(text, date + 6, '.') || !ReadNumber(text, date + 7, 2, 0, 60))
    {
        return NO_MATCH;
    }
    return SkipFraction(text, date + 9);
}

// A date or time field after the anchor.
static Anchor MatchDateTimeField(std::string_view text, std::size_t index) noexcept
{
    if (const std::size_t end = MatchSyslogTimestamp(text, index); end != NO_MATCH && IsBoundary(text, end))
    {
        return {.end = end, .host_next = true};
    }
    for (const auto match : {MatchIsoTimestamp, MatchDashedTimestamp, MatchIsoDate, MatchDottedDate, MatchClock})
    {
        if (const std::size_t end = match(text, index); end != NO_MATCH && IsBoundary(text, end))
        {
            return {.end = end, .host_next = false};
        }
    }
    return {};
}

// 1 to 7 digits.
static std::size_t MatchNumber(std::string_view text, std::size_t index) noexcept
{
    const std::size_t digits = DigitRun(text, index);
    return digits >= 1 && digits <= 7 && IsBoundary(text, index + digits) ? index + digits : NO_MATCH;
}

// An upper-case word of 2 to 10 bytes.
static std::size_t MatchCapsWord(std::string_view text, std::size_t index) noexcept
{
    std::size_t end = index;
    while (end < text.size() && IsUpper(text[end]))
    {
        ++end;
    }
    return end - index >= 2 && end - index <= 10 && IsBoundary(text, end) ? end : NO_MATCH;
}

// A host. After an RFC 3164 timestamp any token that does not end in `:`;
// otherwise letters and digits joined by `-`, `.`, `:`, `/`, or `_`, with at
// least one of each.
static std::size_t MatchHost(std::string_view text, std::size_t index, bool any_token) noexcept
{
    std::size_t end = index;
    bool letter = false;
    bool digit = false;
    while (end < text.size() && !IsBlank(text[end]) && text[end] != '|' && end - index < MAX_FIELD_BYTES)
    {
        const char character = text[end];
        const bool joiner =
            character == '-' || character == '.' || character == ':' || character == '/' || character == '_';
        if (!any_token && !IsLetter(character) && !IsDigit(character) && !joiner)
        {
            return NO_MATCH;
        }
        letter = letter || IsLetter(character);
        digit = digit || IsDigit(character);
        ++end;
    }
    if (end == index || !IsBoundary(text, end) || text[end - 1] == ':')
    {
        return NO_MATCH;
    }
    return any_token || (letter && digit) ? end : NO_MATCH;
}

namespace
{
struct FieldScan
{
    std::size_t position = 0;
    bool host_next = false;
};

enum class FieldKind : std::uint8_t
{
    None,
    // Timestamps, numbers, hosts, and `[...]` fields: cut with the header.
    Noise,
    // Levels, dotted components, and upper-case subsystem words: the message
    // starts here.
    Meaning,
    // A `program[pid]:` tag: the header ends after it.
    Tag
};

struct Field
{
    std::size_t end = NO_MATCH;
    FieldKind kind = FieldKind::None;
};
} // namespace

static Field ReadField(std::string_view text, std::size_t index, FieldScan &scan) noexcept
{
    const bool host_next = std::exchange(scan.host_next, false);
    if (const std::size_t end = MatchLevel(text, index); end != NO_MATCH)
    {
        return {.end = end, .kind = FieldKind::Meaning};
    }
    if (const std::size_t end = MatchTag(text, index); end != NO_MATCH)
    {
        return {.end = end, .kind = FieldKind::Tag};
    }
    if (const Anchor time = MatchDateTimeField(text, index); time.end != NO_MATCH)
    {
        scan.host_next = time.host_next;
        return {.end = time.end, .kind = FieldKind::Noise};
    }
    for (const auto match : {MatchNumber, MatchBracketField})
    {
        if (const std::size_t end = match(text, index); end != NO_MATCH)
        {
            return {.end = end, .kind = FieldKind::Noise};
        }
    }
    for (const auto match : {MatchComponent, MatchCapsWord})
    {
        if (const std::size_t end = match(text, index); end != NO_MATCH)
        {
            return {.end = end, .kind = FieldKind::Meaning};
        }
    }
    if (const std::size_t end = MatchHost(text, index, host_next); end != NO_MATCH)
    {
        return {.end = end, .kind = FieldKind::Noise};
    }
    return {};
}

// A field that ends at `end` can join the header: it and the separator after
// it fit in `MAX_HEADER_BYTES`, and text follows them.
static bool FitsBeforeMessage(std::string_view text, std::size_t end) noexcept
{
    bool pipe = false;
    const std::size_t next = SkipSeparator(text, end, pipe);
    return next <= MAX_HEADER_BYTES && next < text.size();
}

// After an anchor followed by `|`: each field that ends in `|` joins the
// header.
static std::size_t ScanPipeFields(std::string_view text, std::size_t start) noexcept
{
    std::size_t header_end = start;
    std::size_t cursor = start;
    for (std::size_t field = 0; field < MAX_HEADER_FIELDS; ++field)
    {
        std::size_t end = cursor;
        while (end < text.size() && text[end] != '|' && !IsBlank(text[end]) && end - cursor < MAX_PIPE_FIELD)
        {
            ++end;
        }
        if (end == cursor || !At(text, end, '|') || !FitsBeforeMessage(text, end + 1))
        {
            break;
        }
        header_end = end + 1;
        cursor = header_end;
    }
    return header_end;
}

// Reads fields after the anchor and returns where the header ends: after the
// noise fields that come right before the first field with meaning, or after
// the anchor when no such field follows.
static std::size_t ScanFields(std::string_view text, std::size_t anchor_end, bool host_next) noexcept
{
    FieldScan scan{.position = anchor_end, .host_next = host_next};
    for (std::size_t field = 0; field < MAX_HEADER_FIELDS; ++field)
    {
        bool pipe = false;
        const std::size_t start = SkipSeparator(text, scan.position, pipe);
        if (start == scan.position || start >= text.size())
        {
            break;
        }
        if (pipe)
        {
            return field == 0 ? ScanPipeFields(text, start) : anchor_end;
        }
        const Field read = ReadField(text, start, scan);
        if (read.kind == FieldKind::Meaning)
        {
            return scan.position;
        }
        if (read.kind == FieldKind::Tag)
        {
            return FitsBeforeMessage(text, read.end) ? read.end : scan.position;
        }
        if (read.kind == FieldKind::None || !FitsBeforeMessage(text, read.end))
        {
            break;
        }
        scan.position = read.end;
    }
    return anchor_end;
}

// Where the header's fields end, or `NO_MATCH` when no anchor starts the line.
static std::size_t HeaderFieldsEnd(std::string_view line) noexcept
{
    const std::size_t priority = SkipPriority(line);
    if (priority > 0)
    {
        if (const std::size_t end = MatchRfc5424(line, priority); end != NO_MATCH)
        {
            return end;
        }
    }
    std::size_t start = priority;
    if (const std::size_t epoch = SkipLabel(line, start); epoch != NO_MATCH)
    {
        start = epoch;
    }
    else if (const std::size_t timestamp = SkipLogFileName(line, start); timestamp != NO_MATCH)
    {
        start = timestamp;
    }
    const Anchor anchor = MatchAnchor(line, start);
    if (anchor.end == NO_MATCH || !IsBoundary(line, anchor.end))
    {
        return NO_MATCH;
    }
    return ScanFields(line, anchor.end, anchor.host_next);
}

static bool Informative(std::string_view message) noexcept
{
    return std::ranges::any_of(message, [](char character)
                               { return IsLetter(character) || static_cast<unsigned char>(character) >= 0x80; });
}

LineParts SplitLogLine(std::string_view line) noexcept
{
    const LineParts whole{.header = line.substr(0, 0), .message = line};
    const std::size_t fields_end = HeaderFieldsEnd(line);
    if (fields_end == NO_MATCH)
    {
        return whole;
    }
    bool pipe = false;
    const std::size_t header_end = SkipSeparator(line, fields_end, pipe);
    if (header_end > MAX_HEADER_BYTES || !Informative(line.substr(header_end)))
    {
        return whole;
    }
    return {.header = line.substr(0, header_end), .message = line.substr(header_end)};
}

} // namespace nesso::parser

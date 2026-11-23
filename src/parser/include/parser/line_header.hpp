// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#ifndef PARSER_LINE_HEADER_HPP
#define PARSER_LINE_HEADER_HPP

#include <cstddef>
#include <string_view>

namespace nesso::parser
{

/// Version of the rules below, written as the corpus policy `embed.text`
/// value. Bump it whenever a rule changes where a header ends.
inline constexpr std::string_view HEADER_RULE_VERSION = "message-v1";

/// Longest header `SplitLogLine` returns, in bytes, separators included.
inline constexpr std::size_t MAX_HEADER_BYTES = 256;

/// Most header fields `SplitLogLine` reads after the leading timestamp.
inline constexpr std::size_t MAX_HEADER_FIELDS = 8;

/// One log line cut in two. Both views point into the line that was split, and
/// `header` followed by `message` is that line, byte for byte. A later parser
/// pass reads structured fields out of `header`; it does not cut the line
/// again.
struct LineParts
{
    std::string_view header;
    std::string_view message;
};

/// Splits `line` into its leading header and its message. The scan is one
/// left-to-right pass over the bytes. It does not consult a locale, does not
/// use regular expressions, and does not allocate.
///
/// A header starts at byte 0 with an optional prefix, then a timestamp anchor:
///
/// - Prefix: a syslog priority `<N>` (1 to 3 digits); a single label token
///   (`-`, or an upper-case word of 2 to 16 bytes) before an epoch anchor; or
///   a log file name (a token of up to 128 bytes that contains `.log`) before
///   an ISO 8601 date and time.
/// - Anchor: ISO 8601 `YYYY-MM-DD` and `T` or a space and `HH:MM:SS`, with an
///   optional `.` or `,` fraction and an optional `Z` or numeric zone; RFC 3164
///   `Mmm D HH:MM:SS` with an optional year; RFC 5424 `<PRI>1 TIMESTAMP HOST
///   APP PROCID MSGID SD`; one of those, `Www Mmm DD HH:MM:SS YYYY`, or
///   `DD/Mmm/YYYY:HH:MM:SS +zzzz` in square brackets; a 10- or 13-digit epoch
///   followed by a date; `YYMMDD HHMMSS`; or `YYYYMMDD-HH:MM:SS` with optional
///   milliseconds. Months, days, hours, minutes, and seconds are range
///   checked.
///
/// After the anchor the scan reads up to `MAX_HEADER_FIELDS` fields, separated
/// by blanks, a ` - `, or a `|`:
///
/// - Speculative fields: another date or time, a number of 1 to 7 digits, a
///   host (the token after an RFC 3164 timestamp, or letters and digits joined
///   by `-`, `.`, `:`, `/`, or `_`), a `[...]` field, an upper-case word of 2
///   to 10 bytes, or a dotted name such as `app.db`.
/// - A level (`TRACE`, `DEBUG`, `INFO`, `WARN`, `ERROR`, `FATAL`, and the
///   other syslog severities, any case, bare or as `[x]`, `<x>`, or `x:`) ends
///   the header so far. After it, a dotted component and `[...]` fields do too.
/// - A tag, `name`, an optional `(sub)`, an optional `[pid]`, and a `:` before
///   a blank, ends the header and the scan.
/// - After an anchor followed by `|`, each field that ends in `|` ends the
///   header so far.
///
/// Speculative fields join the header only when a later field ends it. The
/// header also takes the separator after its last field. A field joins only
/// when it and that separator fit in `MAX_HEADER_BYTES` and text follows them,
/// so the last token of a line is always in the message.
///
/// The header is empty and the message is the whole line when no anchor starts
/// the line (prose, a leading blank, JSON, `key=value` fields), or when the
/// message would hold no ASCII letter and no byte of 0x80 or above (a header
/// with nothing after it, or only digits and punctuation).
[[nodiscard]] LineParts SplitLogLine(std::string_view line) noexcept;

} // namespace nesso::parser

#endif // PARSER_LINE_HEADER_HPP

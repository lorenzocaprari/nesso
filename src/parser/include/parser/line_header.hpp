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
///   `Mmm D HH:MM:SS` with an optional year; RFC 5424 `<PRI>1 TIMESTAMP HOST`
///   (the rest of an RFC 5424 header must be well formed, and APP-NAME starts
///   the message); one of those, `Www Mmm DD HH:MM:SS YYYY`, or
///   `DD/Mmm/YYYY:HH:MM:SS +zzzz` in square brackets; a 10- or 13-digit epoch
///   followed by a date; `YYMMDD HHMMSS`; or `YYYYMMDD-H:M:S` with optional
///   milliseconds. Months, days, hours, minutes, and seconds are range
///   checked.
///
/// The header holds what says when and where, not what happened. After the
/// anchor the scan reads up to `MAX_HEADER_FIELDS` fields, separated by blanks
/// or a ` - `:
///
/// - Noise fields join the header: another date or time, a number of 1 to 7
///   digits (a pid or thread), a host (the token after an RFC 3164 timestamp,
///   or letters and digits joined by `-`, `.`, `:`, `/`, or `_`), and a `[...]`
///   field of up to 128 bytes.
/// - A program tag ends the header after it: `name`, an optional `(sub)`, an
///   optional `[pid]`, and a `:` before a blank, as in `sshd[42]:`.
/// - A field with meaning starts the message: a level (`TRACE`, `DEBUG`,
///   `INFO`, `WARN`, `ERROR`, `FATAL`, and the other syslog severities, any
///   case, bare or as `[x]`, `<x>`, or `x:`), a dotted component such as
///   `org.apache.Main`, or an upper-case subsystem word of 2 to 10 bytes such
///   as `KERNEL`. A tag after it stays in the message too.
/// - After an anchor followed by `|`, each field that ends in `|` joins the
///   header. A `|` after a noise field ends the header at the anchor.
///
/// Noise fields join the header only when a tag or a field with meaning
/// follows them; otherwise the header is the anchor alone. The header also
/// takes the separator after its last field. A field joins only when it and
/// that separator fit in `MAX_HEADER_BYTES` and text follows them.
///
/// The header is empty and the message is the whole line when no anchor starts
/// the line (prose, a leading blank, JSON, `key=value` fields), or when the
/// message would hold no ASCII letter and no byte of 0x80 or above (a header
/// with nothing after it, or only digits and punctuation).
[[nodiscard]] auto SplitLogLine(std::string_view line) noexcept -> LineParts;

} // namespace nesso::parser

#endif // PARSER_LINE_HEADER_HPP

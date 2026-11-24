// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#ifndef PARSER_TEMPLATE_MASK_HPP
#define PARSER_TEMPLATE_MASK_HPP

#include <string>
#include <string_view>

namespace nesso::parser
{

/// Placeholder written in place of one variable token.
inline constexpr std::string_view MASKED_TOKEN = "<*>";

/// Version of the rules below, written as the corpus policy `mask` value.
/// Bump it whenever a rule changes which tokens are masked.
inline constexpr std::string_view MASK_RULE_VERSION = "template-v2";

/// Dedup key for `text`. Variable tokens are replaced with `MASKED_TOKEN`.
///
/// The scan is one left-to-right pass over the bytes. It does not consult a
/// locale and it does not use regular expressions. A token is masked only when
/// it matches a class below. Every other byte is copied.
///
/// - Decimal integer, with an optional leading `+` or `-`, bounded by a
///   non-word byte. Word bytes are `A-Z`, `a-z`, `0-9`, and `_`.
/// - Hex run of 6 or more `0-9A-Fa-f` that contains a letter `A-F`, or `0x` /
///   `0X` followed by 6 or more hex digits.
/// - Hyphenated UUID, `8-4-4-4-12` hex digits.
/// - IPv4, each octet `0-255`, with an optional `:port` in `0-65535`.
/// - IPv6, including one `::` compression and an IPv4 tail. A port is accepted
///   only as `[address]:port`.
/// - Absolute path that contains a digit. The path starts with `/` when the
///   previous byte is not a path byte (`A-Z`, `a-z`, `0-9`, `_`, `/`, `.`,
///   `-`, `+`, `~`, `%`).
/// - HDFS id: `blk_`, an optional `-`, then digits.
/// - Mixed token, tried after every other class: three or more bytes of word
///   characters joined by single
///   `.`, `:`, `@`, `/`, or `-`, containing at least one ASCII letter and one
///   digit, that does not follow a word byte or joiner. Trailing joiners are not
///   part of it. This covers host and node names (`dn228`,
///   `R02-M1-N0-C:J12-U11`), sizes and durations (`126MB`, `30000ms`), short
///   hex (`0x01`), and versioned words such as `http2` or `sha256`. The key
///   only decides which lines share one embedding; lexical scoring still sees
///   each line's own text.
/// - Timestamp: ISO 8601 `YYYY-MM-DD` with an optional `T` or space,
/// `HH:MM:SS`,
///   optional fraction, and optional `Z` or numeric zone; syslog
///   `Mon DD HH:MM:SS`; or a standalone `HH:MM:SS` with an optional `.fff`.
///
/// Near-misses stay literal. A 5-character hex run with no digit (`abcde`) and
/// a two-byte word (`v2`, `S3`) are not variables. A dot leader ends a mixed
/// token, so `dcbf.........0` keeps `dcbf`. `BLK_123` and `myblk_123` are not
/// HDFS ids; they are masked as mixed tokens. A path with no digit
/// (`/var/log/app.log`) stays. A relative path that starts with a word byte
/// and contains a digit (`foo/bar2`) is one mixed token; `~/file1` stays.
/// Three octets (`1.2.3`), an octet above 255, and `12:34` are not one
/// address or one clock; `12:34` becomes `<*>:<*>` because each number is a
/// decimal. `std::vector` is not an IPv6 address.
[[nodiscard]] auto MaskTemplate(std::string_view text) -> std::string;

} // namespace nesso::parser

#endif // PARSER_TEMPLATE_MASK_HPP

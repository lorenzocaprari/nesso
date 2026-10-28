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
/// - Timestamp: ISO 8601 `YYYY-MM-DD` with an optional `T` or space, `HH:MM:SS`,
///   optional fraction, and optional `Z` or numeric zone; syslog
///   `Mon DD HH:MM:SS`; or a standalone `HH:MM:SS` with an optional `.fff`.
///
/// Near-misses stay literal. A 5-character hex run (`abcde`), `0x` plus fewer
/// than 6 hex digits (`0xabcde`), and a word that mixes digits with a non-hex
/// letter (`http2`, `sha256`, `log4j`, `30s`, `x86_64`) are not variables.
/// `BLK_123` and `myblk_123` are not HDFS ids. A path with no digit
/// (`/var/log/app.log`) and a relative path (`foo/bar2`, `~/file1`) stay.
/// Three octets (`1.2.3`), an octet above 255, and `12:34` are not one
/// address or one clock; `12:34` becomes `<*>:<*>` because each number is a
/// decimal. `std::vector` is not an IPv6 address.
[[nodiscard]] std::string maskTemplate(std::string_view text);

} // namespace nesso::parser

#endif // PARSER_TEMPLATE_MASK_HPP

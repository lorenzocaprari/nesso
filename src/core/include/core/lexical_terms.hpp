// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#ifndef CORE_LEXICAL_TERMS_HPP
#define CORE_LEXICAL_TERMS_HPP

#include <string>
#include <string_view>
#include <vector>

namespace nesso::core
{

/// Rule-set version recorded with these terms. `v1` is the rules below.
inline constexpr std::string_view LEXICAL_TERMS_VERSION{"v1"};

/// Terms of `text` under `LEXICAL_TERMS_VERSION`.
///
/// ASCII `A`-`Z` is lowercased. A term byte is an ASCII letter, an ASCII digit,
/// or any byte `>= 0x80`. Every other byte is a separator. The scan is written
/// out by hand and does not use `std::regex`.
///
/// CamelCase boundaries are decided on the original bytes, before lowercasing.
/// Lowercasing first would erase the boundary. Inside one run of term bytes, a
/// boundary falls before an ASCII uppercase letter that follows an ASCII
/// lowercase letter (`fooBar` -> `foo`, `bar`). An uppercase run is not split,
/// so `XMLParser` stays `xmlparser` and `IPv4` stays `ipv4`. Digits and bytes
/// `>= 0x80` do not open a boundary. A single leading capital (`Ab`) is not a
/// boundary. The unsplit run is not kept; only the pieces are.
///
/// A maximal sequence of two or more runs separated only by `-`, `_`, `.`, or
/// `:` is also kept as one compound term. The compound is that original span
/// with ASCII lowercased, and it is emitted after the pieces of the runs
/// (`blk_-123`, `10.0.0.1`). Any other separator ends the sequence.
///
/// Order is left to right. Duplicates are kept, one entry per occurrence, so
/// the result is a multiset: `a-a` is `a`, `a`, `a-a`.
[[nodiscard]] std::vector<std::string> lexicalTerms(std::string_view text);

} // namespace nesso::core

#endif // CORE_LEXICAL_TERMS_HPP

// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#ifndef CORE_QUERY_CLASS_HPP
#define CORE_QUERY_CLASS_HPP

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace nesso::core
{

/// Query class used by `--mode auto` routing ([ADR 0011](docs/adr/0011-hybrid-search-and-identifier-routing.md)).
enum class QueryClass : std::uint8_t
{
    Identifier,
    Mixed,
    NaturalLanguage
};

/// Tokens after quote stripping and ASCII whitespace split, plus the substring
/// used for lexical exact-match reordering (outer quotes stripped once).
struct QueryTokens
{
    std::vector<std::string> tokens;
    std::string stripped;
};

/// Strip one surrounding `"…"` pair from the whole query when present, split on
/// ASCII whitespace (`0x09`, `0x0A`, `0x0D`, `0x20`), drop empty tokens, and strip
/// one surrounding `"…"` pair from each token.
[[nodiscard]] QueryTokens tokenizeQuery(std::string_view query);

/// True when `token` fully matches one identifier pattern from ADR 0011.
[[nodiscard]] bool isIdentifierToken(std::string_view token);

/// Classify a query. Empty after tokenization is natural language.
[[nodiscard]] QueryClass classifyQuery(std::string_view query);

} // namespace nesso::core

#endif // CORE_QUERY_CLASS_HPP

// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#ifndef NESSO_RENDER_JSON_HPP
#define NESSO_RENDER_JSON_HPP

#include <nesso.hpp>

#include <cstddef>
#include <string>

namespace nesso::cli
{

/// One JSON Lines object. Keys, in order: schema_version, rank, source, kind, line, score, text, occurrences.
/// `schema_version` is 1. Invalid UTF-8 in `source` and `text` becomes U+FFFD.
[[nodiscard]] std::string renderMatchJson(const TextMatch &match, std::size_t rank);

} // namespace nesso::cli

#endif // NESSO_RENDER_JSON_HPP

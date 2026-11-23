// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#ifndef NESSO_RENDER_JSON_HPP
#define NESSO_RENDER_JSON_HPP

#include <nesso/nesso.hpp>

#include <cstddef>
#include <string>

namespace nesso::cli
{

/// One JSON Lines object. Keys, in order: schema_version, rank, source, kind,
/// line, score, text, occurrences, mode, dense, lexical. `schema_version` is 2.
/// `score` is the shortest round-trip form of the float, independent of locale.
/// `dense` and `lexical` are null when not computed. Invalid UTF-8 in `source`
/// and `text` becomes U+FFFD.
[[nodiscard]] std::string RenderMatchJson(const TextMatch &match, std::size_t rank);

/// `RenderMatchJson` with `,"final":false,"refresh":N` before the closing
/// brace. `refresh` counts provisional updates from 1. Final records have no
/// `final` key.
[[nodiscard]] std::string RenderProvisionalMatchJson(const TextMatch &match, std::size_t rank, std::size_t refresh);

} // namespace nesso::cli

#endif // NESSO_RENDER_JSON_HPP

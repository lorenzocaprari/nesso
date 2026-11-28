// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#ifndef NESSO_RENDER_TEXT_HPP
#define NESSO_RENDER_TEXT_HPP

#include <nesso/nesso.hpp>
#include <nesso/overview.hpp>

#include <string>
#include <string_view>

namespace nesso::cli
{

/// Escapes control characters and invalid UTF-8 bytes for safe terminal output.
[[nodiscard]] auto EscapeTerminalControls(std::string_view text) -> std::string;

/// `Message(error)` with the stored and loaded values of a model mismatch
/// escaped by `EscapeTerminalControls`.
[[nodiscard]] auto FormatErrorMessage(const Error &error) -> std::string;

/// One text row for `match`, without the newline: `[SOURCE:]line N[ (COUNT)]:
/// SCORE: TEXT`. The source prefix is printed for several sources, stdin, and
/// archive members. `--group` results add the count. Source and text are
/// escaped.
[[nodiscard]] auto FormatTextRow(const TextResults &results, const TextMatch &match) -> std::string;

/// One `overview` row, without the newline: `SOURCE:line N (COUNT): TEXT`, or
/// `element N` for a JSON array slot. The source is always printed, so the row
/// cites its first line. Source and text are escaped. There is no score.
[[nodiscard]] auto FormatEventTypeRow(const EventTypeRow &row) -> std::string;

} // namespace nesso::cli

#endif // NESSO_RENDER_TEXT_HPP

// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#ifndef NESSO_RENDER_TEXT_HPP
#define NESSO_RENDER_TEXT_HPP

#include <string>
#include <string_view>

namespace nesso::cli
{

/// Escapes control characters and invalid UTF-8 bytes for safe terminal output.
[[nodiscard]] std::string escapeTerminalControls(std::string_view text);

} // namespace nesso::cli

#endif // NESSO_RENDER_TEXT_HPP

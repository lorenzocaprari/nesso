// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#ifndef PLATFORM_TERMINAL_HPP
#define PLATFORM_TERMINAL_HPP

#include <cstddef>
#include <cstdint>

namespace nesso::platform
{

/// True when stderr is attached to an interactive terminal.
[[nodiscard]] auto IsStderrTerminal() noexcept -> bool;

/// Columns of the terminal on stderr, or 80 when the width cannot be read.
[[nodiscard]] auto StderrTerminalColumns() noexcept -> std::size_t;

/// Columns of the terminal on `descriptor`, or 80 when the width cannot be read.
[[nodiscard]] auto TerminalColumns(int descriptor) noexcept -> std::size_t;

/// Current process id.
[[nodiscard]] auto ProcessId() noexcept -> std::int64_t;

} // namespace nesso::platform

#endif // PLATFORM_TERMINAL_HPP

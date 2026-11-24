// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <platform/terminal.hpp>

#include <cstdint>
#include <cstdio>
#include <io.h>
#include <windows.h>

namespace nesso::platform
{

inline constexpr std::size_t FALLBACK_TERMINAL_WIDTH = 80;

auto IsStderrTerminal() noexcept -> bool { return _isatty(_fileno(stderr)) != 0; }

auto TerminalColumns(int descriptor) noexcept -> std::size_t
{
    const intptr_t handle = _get_osfhandle(descriptor);
    if (handle == -1 || handle == reinterpret_cast<intptr_t>(INVALID_HANDLE_VALUE))
    {
        return FALLBACK_TERMINAL_WIDTH;
    }
    CONSOLE_SCREEN_BUFFER_INFO info{};
    if (GetConsoleScreenBufferInfo(reinterpret_cast<HANDLE>(handle), &info) == 0)
    {
        return FALLBACK_TERMINAL_WIDTH;
    }
    const SHORT width = static_cast<SHORT>(info.srWindow.Right - info.srWindow.Left + 1);
    return width > 0 ? static_cast<std::size_t>(width) : FALLBACK_TERMINAL_WIDTH;
}

auto StderrTerminalColumns() noexcept -> std::size_t { return TerminalColumns(_fileno(stderr)); }

auto ProcessId() noexcept -> std::int64_t { return static_cast<std::int64_t>(GetCurrentProcessId()); }

} // namespace nesso::platform

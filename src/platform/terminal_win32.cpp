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

auto IsStderrTerminal() noexcept -> bool
{
    const int descriptor = _fileno(stderr);
    if (_isatty(descriptor) == 0)
    {
        return false;
    }
    const intptr_t raw = _get_osfhandle(descriptor);
    if (raw == -1 || raw == reinterpret_cast<intptr_t>(INVALID_HANDLE_VALUE))
    {
        return false;
    }
    const auto handle = reinterpret_cast<HANDLE>(raw);
    DWORD mode = 0;
    if (GetConsoleMode(handle, &mode) == 0)
    {
        return false;
    }
    // Live view and progress need VT sequences. Enable them when missing;
    // without VT, treat stderr as a non-terminal so the plain path is used.
    if ((mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) == 0 &&
        SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) == 0)
    {
        return false;
    }
    return true;
}

auto TerminalColumns(int descriptor) noexcept -> std::size_t
{
    if (descriptor < 0)
    {
        return FALLBACK_TERMINAL_WIDTH;
    }
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

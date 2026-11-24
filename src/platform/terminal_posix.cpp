// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <platform/terminal.hpp>

#include <cstddef>
#include <cstdint>
#include <sys/ioctl.h>
#include <unistd.h>

namespace nesso::platform
{

inline constexpr std::size_t FALLBACK_TERMINAL_WIDTH = 80;

auto IsStderrTerminal() noexcept -> bool { return ::isatty(STDERR_FILENO) != 0; }

auto TerminalColumns(int descriptor) noexcept -> std::size_t
{
    winsize size{};
    // TIOCGWINSZ comes from <sys/ioctl.h> through kernel headers.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg, hicpp-vararg, misc-include-cleaner)
    if (::ioctl(descriptor, TIOCGWINSZ, &size) == 0 && size.ws_col > 0)
    {
        return size.ws_col;
    }
    return FALLBACK_TERMINAL_WIDTH;
}

auto StderrTerminalColumns() noexcept -> std::size_t { return TerminalColumns(STDERR_FILENO); }

auto ProcessId() noexcept -> std::int64_t { return static_cast<std::int64_t>(::getpid()); }

} // namespace nesso::platform

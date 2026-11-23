// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <cli/live_view.hpp>
#include <cli/render_text.hpp>

#include <nesso/nesso.hpp>

#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <sys/ioctl.h>
#include <unistd.h>
#include <utility>

namespace nesso::cli
{

bool LiveViewEnabled(const LiveViewInputs &inputs) noexcept
{
    if (inputs.requested.has_value() && !*inputs.requested)
    {
        return false;
    }
    if (inputs.quiet || !inputs.stderr_is_terminal || inputs.term == nullptr)
    {
        return false;
    }
    const std::string_view term{inputs.term};
    return !term.empty() && term != "dumb";
}

std::size_t TerminalWidth(int descriptor) noexcept
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

std::string_view CutToColumns(std::string_view text, std::size_t columns) noexcept
{
    std::size_t count = 0;
    for (std::size_t index = 0; index < text.size(); ++index)
    {
        const auto byte = static_cast<unsigned char>(text[index]);
        if ((byte & 0xC0U) == 0x80U)
        {
            continue;
        }
        if (count == columns)
        {
            return text.substr(0, index);
        }
        ++count;
    }
    return text;
}

LiveView::LiveView(std::FILE *out, Clock clock, std::size_t width) : out_(out), clock_(std::move(clock)), width_(width)
{
}

void LiveView::MoveToTop() const noexcept
{
    // \r to column 1, CSI n F up n rows to column 1, CSI J clear to the end of
    // the screen. That clears the block and the progress row under it.
    static_cast<void>(std::fputc('\r', out_));
    if (rows_drawn_ > 0)
    {
        std::array<char, 32> buffer{};
        const std::to_chars_result result = std::to_chars(buffer.data(), std::to_address(buffer.end()), rows_drawn_);
        static_cast<void>(std::fputs("\x1b[", out_));
        static_cast<void>(std::fwrite(buffer.data(), 1, static_cast<std::size_t>(result.ptr - buffer.data()), out_));
        static_cast<void>(std::fputc('F', out_));
    }
    static_cast<void>(std::fputs("\x1b[J", out_));
}

void LiveView::OnProvisional(const TextResults &results, size_t lines_done, size_t lines_total)
{
    const std::chrono::steady_clock::time_point now = clock_();
    if (last_draw_ && now - *last_draw_ < LIVE_VIEW_REDRAW_INTERVAL)
    {
        return;
    }
    last_draw_ = now;
    const std::size_t columns = width_ > 0 ? width_ - 1 : 0;
    const std::string header =
        std::format("Provisional (dense) top-{} after {}/{} lines:", results.matches.size(), lines_done, lines_total);
    std::string block{CutToColumns(header, columns)};
    block.push_back('\n');
    for (const TextMatch &match : results.matches)
    {
        const std::string row = FormatTextRow(results, match);
        block.append(CutToColumns(row, columns));
        block.push_back('\n');
    }
    MoveToTop();
    static_cast<void>(std::fwrite(block.data(), 1, block.size(), out_));
    static_cast<void>(std::fflush(out_));
    rows_drawn_ = results.matches.size() + 1;
}

void LiveView::OnScanDone() noexcept
{
    if (rows_drawn_ == 0)
    {
        return;
    }
    MoveToTop();
    static_cast<void>(std::fflush(out_));
    rows_drawn_ = 0;
}

std::unique_ptr<LiveView> MakeStderrLiveView()
{
    return std::make_unique<LiveView>(
        stderr, [] { return std::chrono::steady_clock::now(); }, TerminalWidth(STDERR_FILENO));
}

} // namespace nesso::cli

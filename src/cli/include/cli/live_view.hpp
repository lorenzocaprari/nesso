// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#ifndef NESSO_LIVE_VIEW_HPP
#define NESSO_LIVE_VIEW_HPP

#include <nesso/nesso.hpp>

#include <chrono>
#include <cstddef>
#include <cstdio>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace nesso::cli
{

/// Columns used when the terminal width cannot be read.
inline constexpr std::size_t FALLBACK_TERMINAL_WIDTH = 80;

/// Shortest time between two redraws of the view.
inline constexpr std::chrono::milliseconds LIVE_VIEW_REDRAW_INTERVAL{100};

/// What decides whether `grep` draws provisional results on stderr.
struct LiveViewInputs
{
    /// `--provisional` (true), `--no-provisional` (false), or neither.
    std::optional<bool> requested;
    bool quiet = false;
    bool stderr_is_terminal = false;
    /// `TERM`, or null when it is unset.
    const char *term = nullptr;
};

/// True when stderr is a terminal, `TERM` is set and not `dumb`, `--quiet` is
/// off, and `--no-provisional` was not given.
[[nodiscard]] bool LiveViewEnabled(const LiveViewInputs &inputs) noexcept;

/// Columns of the terminal on `descriptor`, or `FALLBACK_TERMINAL_WIDTH`.
[[nodiscard]] std::size_t TerminalWidth(int descriptor) noexcept;

/// `text` cut to at most `columns` code points. `text` is valid UTF-8 after
/// `EscapeTerminalControls`, so a character is never split.
[[nodiscard]] std::string_view CutToColumns(std::string_view text, std::size_t columns) noexcept;

/// Draws the provisional top-k as a block of rows above the progress line and
/// erases it when the scan ends, so the terminal ends as it would without it.
/// Rows use the final text format, cut to the terminal width minus one.
class LiveView final : public GrepObserver
{
  public:
    using Clock = std::function<std::chrono::steady_clock::time_point()>;

    /// Writes to `out`. `clock` paces redraws. `width` is the terminal width.
    LiveView(std::FILE *out, Clock clock, std::size_t width);

    void OnProvisional(const TextResults &results, size_t lines_done, size_t lines_total) override;
    void OnScanDone() noexcept override;

  private:
    void MoveToTop() const noexcept;

    std::FILE *out_;
    Clock clock_;
    std::size_t width_;
    std::size_t rows_drawn_ = 0;
    std::optional<std::chrono::steady_clock::time_point> last_draw_;
};

/// A view on stderr with the steady clock and the stderr terminal width.
[[nodiscard]] std::unique_ptr<LiveView> MakeStderrLiveView();

} // namespace nesso::cli

#endif // NESSO_LIVE_VIEW_HPP

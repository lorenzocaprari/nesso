// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_all.hpp>
#include <cli/live_view.hpp>
#include <nesso/nesso.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <fcntl.h>
#include <optional>
#include <string>
#include <string_view>
#include <unistd.h>

namespace
{

using TimePoint = std::chrono::steady_clock::time_point;
using std::chrono::milliseconds;

// Collects what the view writes, through a FILE* like stderr.
class Capture
{
  public:
    Capture() : stream_(std::tmpfile()) { REQUIRE(stream_ != nullptr); }
    Capture(const Capture &) = delete;
    Capture &operator=(const Capture &) = delete;
    Capture(Capture &&) = delete;
    Capture &operator=(Capture &&) = delete;
    ~Capture() { static_cast<void>(std::fclose(stream_)); }

    [[nodiscard]] std::FILE *Stream() const { return stream_; }

    // Bytes written since the previous call.
    [[nodiscard]] std::string Next()
    {
        REQUIRE(std::fflush(stream_) == 0);
        REQUIRE(std::fseek(stream_, taken_, SEEK_SET) == 0);
        std::string fresh;
        for (int byte = std::fgetc(stream_); byte != EOF; byte = std::fgetc(stream_))
        {
            fresh.push_back(static_cast<char>(byte));
        }
        taken_ += static_cast<long>(fresh.size());
        REQUIRE(std::fseek(stream_, 0, SEEK_END) == 0);
        return fresh;
    }

  private:
    std::FILE *stream_;
    long taken_ = 0;
};

class FakeClock
{
  public:
    [[nodiscard]] nesso::cli::LiveView::Clock Function()
    {
        return [this] { return now_; };
    }

    void Advance(milliseconds step) { now_ += step; }

  private:
    TimePoint now_;
};

nesso::TextResults TwoRows()
{
    nesso::TextResults results;
    results.matches.push_back({.source = "app.log", .line = 3, .score = 0.75F, .text = "disk full"});
    results.matches.push_back({.source = "app.log", .line = 9, .score = 0.5F, .text = "disk nearly full"});
    return results;
}

constexpr std::string_view FIRST_BLOCK = "Provisional (dense) top-2 after 1024/2000 lines:\n"
                                         "line 3: 0.7500: disk full\n"
                                         "line 9: 0.5000: disk nearly full\n";

} // namespace

TEST_CASE("the first draw clears the progress row and prints the block", "[live_view]")
{
    Capture capture;
    FakeClock clock;
    nesso::cli::LiveView view{capture.Stream(), clock.Function(), 80};

    view.OnProvisional(TwoRows(), 1024, 2000);
    CHECK(capture.Next() == std::string{"\r\x1b[J"} + std::string{FIRST_BLOCK});
}

TEST_CASE("a redraw moves up over the previous block and clears it", "[live_view]")
{
    Capture capture;
    FakeClock clock;
    nesso::cli::LiveView view{capture.Stream(), clock.Function(), 80};

    view.OnProvisional(TwoRows(), 1024, 2000);
    static_cast<void>(capture.Next());
    clock.Advance(milliseconds{100});
    nesso::TextResults one;
    one.matches.push_back({.source = "app.log", .line = 1500, .score = 0.875F, .text = "the disk is full"});
    view.OnProvisional(one, 2000, 2000);
    CHECK(capture.Next() == "\r\x1b[3F\x1b[JProvisional (dense) top-1 after 2000/2000 lines:\n"
                            "line 1500: 0.8750: the disk is full\n");

    clock.Advance(milliseconds{100});
    view.OnProvisional(TwoRows(), 1024, 2000);
    CHECK(capture.Next().starts_with("\r\x1b[2F\x1b[J"));
}

TEST_CASE("scan done erases the block once", "[live_view]")
{
    Capture capture;
    FakeClock clock;
    nesso::cli::LiveView view{capture.Stream(), clock.Function(), 80};

    view.OnProvisional(TwoRows(), 1024, 2000);
    static_cast<void>(capture.Next());
    view.OnScanDone();
    CHECK(capture.Next() == "\r\x1b[3F\x1b[J");
    view.OnScanDone();
    CHECK(capture.Next().empty());
}

TEST_CASE("a view that never drew writes nothing when the scan ends", "[live_view]")
{
    Capture capture;
    FakeClock clock;
    nesso::cli::LiveView view{capture.Stream(), clock.Function(), 80};

    view.OnScanDone();
    CHECK(capture.Next().empty());
}

TEST_CASE("redraws are at most one per interval", "[live_view]")
{
    Capture capture;
    FakeClock clock;
    nesso::cli::LiveView view{capture.Stream(), clock.Function(), 80};

    view.OnProvisional(TwoRows(), 1024, 4096);
    CHECK_FALSE(capture.Next().empty());
    clock.Advance(milliseconds{50});
    view.OnProvisional(TwoRows(), 2048, 4096);
    CHECK(capture.Next().empty());
    clock.Advance(milliseconds{49});
    view.OnProvisional(TwoRows(), 3072, 4096);
    CHECK(capture.Next().empty());
    clock.Advance(milliseconds{1});
    view.OnProvisional(TwoRows(), 4096, 4096);
    CHECK(capture.Next() == "\r\x1b[3F\x1b[JProvisional (dense) top-2 after 4096/4096 lines:\n"
                            "line 3: 0.7500: disk full\n"
                            "line 9: 0.5000: disk nearly full\n");
}

TEST_CASE("rows are cut to the width minus one by code point", "[live_view]")
{
    CHECK(nesso::cli::CutToColumns("a\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80z", 3) == "a\xC3\xA9\xE2\x82\xAC");
    CHECK(nesso::cli::CutToColumns("a\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80z", 4) ==
          "a\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80");
    CHECK(nesso::cli::CutToColumns("abc", 3) == "abc");
    CHECK(nesso::cli::CutToColumns("abc", 0).empty());

    Capture capture;
    FakeClock clock;
    nesso::cli::LiveView view{capture.Stream(), clock.Function(), 12};
    nesso::TextResults results;
    results.matches.push_back(
        {.source = "app.log", .line = 7, .score = 0.5F, .text = "\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9"});
    view.OnProvisional(results, 1, 1);
    // "line 7: 0.5000: " is 16 columns, so 11 columns keep "line 7: 0.5".
    CHECK(capture.Next() == "\r\x1b[JProvisional\nline 7: 0.5\n");

    nesso::cli::LiveView accented{capture.Stream(), clock.Function(), 20};
    results.matches.front().line = 1;
    accented.OnProvisional(results, 1, 1);
    // 19 columns: "line 1: 0.5000: " is 16, then three two-byte characters.
    CHECK(capture.Next() == "\r\x1b[JProvisional (dense)\nline 1: 0.5000: \xC3\xA9\xC3\xA9\xC3\xA9\n");

    nesso::cli::LiveView narrow{capture.Stream(), clock.Function(), 0};
    narrow.OnProvisional(results, 1, 1);
    CHECK(capture.Next() == "\r\x1b[J\n\n");
}

TEST_CASE("rows escape terminal controls from the log text", "[live_view]")
{
    Capture capture;
    FakeClock clock;
    nesso::cli::LiveView view{capture.Stream(), clock.Function(), 200};
    nesso::TextResults results;
    results.matches.push_back({.source = "app.log", .line = 1, .score = 1.0F, .text = "a\x1b[2J\x07z"});
    view.OnProvisional(results, 1, 1);
    const std::string written = capture.Next();
    CHECK(written.contains("line 1: 1.0000: a\\x1B[2J\\x07z\n"));
    // The only escape byte is the view's own clear sequence.
    CHECK(std::ranges::count(written, '\x1b') == 1);
    CHECK(!written.contains('\x07'));
}

TEST_CASE("the view is on only for an interactive terminal", "[live_view]")
{
    using nesso::cli::LiveViewEnabled;
    const auto inputs = [](std::optional<bool> requested, bool quiet, bool terminal, const char *term)
    {
        return nesso::cli::LiveViewInputs{
            .requested = requested, .quiet = quiet, .stderr_is_terminal = terminal, .term = term};
    };

    CHECK(LiveViewEnabled(inputs(std::nullopt, false, true, "xterm-256color")));
    CHECK(LiveViewEnabled(inputs(true, false, true, "xterm")));
    CHECK_FALSE(LiveViewEnabled(inputs(false, false, true, "xterm")));
    CHECK_FALSE(LiveViewEnabled(inputs(std::nullopt, true, true, "xterm")));
    CHECK_FALSE(LiveViewEnabled(inputs(true, true, true, "xterm")));
    CHECK_FALSE(LiveViewEnabled(inputs(std::nullopt, false, false, "xterm")));
    CHECK_FALSE(LiveViewEnabled(inputs(true, false, false, "xterm")));
    CHECK_FALSE(LiveViewEnabled(inputs(std::nullopt, false, true, nullptr)));
    CHECK_FALSE(LiveViewEnabled(inputs(true, false, true, nullptr)));
    CHECK_FALSE(LiveViewEnabled(inputs(std::nullopt, false, true, "")));
    CHECK_FALSE(LiveViewEnabled(inputs(std::nullopt, false, true, "dumb")));
    CHECK_FALSE(LiveViewEnabled(inputs(true, false, true, "dumb")));
}

TEST_CASE("the width falls back to 80 columns off a terminal", "[live_view]")
{
    std::array<int, 2> pipe_ends{};
    REQUIRE(::pipe2(pipe_ends.data(), O_CLOEXEC) == 0);
    CHECK(nesso::cli::TerminalWidth(pipe_ends[0]) == nesso::cli::FALLBACK_TERMINAL_WIDTH);
    static_cast<void>(::close(pipe_ends[0]));
    static_cast<void>(::close(pipe_ends[1]));
    CHECK(nesso::cli::TerminalWidth(-1) == nesso::cli::FALLBACK_TERMINAL_WIDTH);
    CHECK(nesso::cli::MakeStderrLiveView() != nullptr);
}

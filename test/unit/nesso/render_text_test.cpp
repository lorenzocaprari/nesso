// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_all.hpp>
#include <cli/render_text.hpp>
#include <nesso/nesso.hpp>

#include <string>

TEST_CASE("terminal text escapes ASCII controls", "[text][terminal]")
{
    std::string input = "plain";
    input.push_back('\0');
    input.push_back('\t');
    input.push_back('\r');
    input.append("\x1B[2J");
    input.push_back('\x07');
    input.push_back('\x7F');

    CHECK(nesso::cli::EscapeTerminalControls(input) == "plain\\x00\\x09\\x0D\\x1B[2J\\x07\\x7F");
}

TEST_CASE("terminal text escapes C1 controls and invalid UTF-8", "[text][terminal]")
{
    std::string input = "c1:";
    input.append("\xC2\x9B");
    input.append(",valid:");
    input.append("\xC3\xA9");
    input.append(",invalid:");
    input.push_back('\x9B');
    input.append("\xC0\x80");

    CHECK(nesso::cli::EscapeTerminalControls(input) == "c1:\\u009B,valid:\xC3\xA9,invalid:\\x9B\\xC0\\x80");
}

TEST_CASE("text rows keep the four final formats byte for byte", "[text][row]")
{
    nesso::TextResults results;
    const nesso::TextMatch line{.source = "app.log",
                                .line = 12,
                                .score = 0.81234F,
                                .text = "disk full",
                                .array_element = false,
                                .occurrences = 3};
    const nesso::TextMatch element{
        .source = "rows.json", .line = 4, .score = -0.25F, .text = "only", .array_element = true, .occurrences = 1};

    CHECK(nesso::cli::FormatTextRow(results, line) == "line 12: 0.8123: disk full");
    CHECK(nesso::cli::FormatTextRow(results, element) == "element 4: -0.2500: only");

    results.multiple_sources = true;
    CHECK(nesso::cli::FormatTextRow(results, line) == "app.log:line 12: 0.8123: disk full");

    results.grouped = true;
    CHECK(nesso::cli::FormatTextRow(results, line) == "app.log:line 12 (3): 0.8123: disk full");

    results.multiple_sources = false;
    CHECK(nesso::cli::FormatTextRow(results, line) == "line 12 (3): 0.8123: disk full");
}

TEST_CASE("text rows prefix stdin and archive members and escape both fields", "[text][row]")
{
    const nesso::TextResults results;
    const nesso::TextMatch from_stdin{
        .source = "-", .line = 1, .score = 1.0F, .text = "a\x1B[2Jb", .array_element = false, .occurrences = 1};
    const nesso::TextMatch member{.source = "bundle.tgz:var/log/\x07syslog",
                                  .line = 2,
                                  .score = 0.5F,
                                  .text = "tab\there",
                                  .array_element = false,
                                  .occurrences = 1};

    CHECK(nesso::cli::FormatTextRow(results, from_stdin) == "-:line 1: 1.0000: a\\x1B[2Jb");
    CHECK(nesso::cli::FormatTextRow(results, member) == "bundle.tgz:var/log/\\x07syslog:line 2: 0.5000: tab\\x09here");
}

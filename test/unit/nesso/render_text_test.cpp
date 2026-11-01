// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_all.hpp>
#include <render_text.hpp>

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

    CHECK(nesso::cli::escapeTerminalControls(input) == "plain\\x00\\x09\\x0D\\x1B[2J\\x07\\x7F");
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

    CHECK(nesso::cli::escapeTerminalControls(input) == "c1:\\u009B,valid:\xC3\xA9,invalid:\\x9B\\xC0\\x80");
}

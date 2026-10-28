// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_all.hpp>
#include <render_json.hpp>

#include <string>

TEST_CASE("json rows keep schema version 1 and append occurrences", "[json]")
{
    const nesso::TextMatch first{
        .source = "app.log", .line = 2, .score = 0.5F, .text = "same", .arrayElement = false, .occurrences = 3};
    const nesso::TextMatch second{
        .source = "rows.json", .line = 4, .score = -0.25F, .text = "only", .arrayElement = true, .occurrences = 1};

    CHECK(
        nesso::cli::renderMatchJson(first, 1) ==
        R"({"schema_version":1,"rank":1,"source":"app.log","kind":"line","line":2,"score":0.5,"text":"same","occurrences":3})");
    CHECK(
        nesso::cli::renderMatchJson(second, 2) ==
        R"({"schema_version":1,"rank":2,"source":"rows.json","kind":"element","line":4,"score":-0.25,"text":"only","occurrences":1})");
}

TEST_CASE("json escapes controls and replaces invalid utf-8", "[json]")
{
    std::string text;
    text.push_back('"');
    text.push_back('\\');
    text.push_back('\b');
    text.push_back('\f');
    text.push_back('\n');
    text.push_back('\r');
    text.push_back('\t');
    text.push_back('\x01');
    text.push_back('\x0E');
    text.append("ok ");
    text.append("\xC3\xA9");
    text.append("\xE2\x82\xAC");
    text.append("\xF0\x9F\x98\x80");
    text.push_back('\xFF');
    text.append("\xC3 ");
    text.append("\xC0\x80");
    text.append("\xED\xA0\x80");
    text.append("\xF4\x90\x80\x80");
    text.push_back('\xC3');

    const nesso::TextMatch match{
        .source = "a\xFF", .line = 1, .score = 1.0F, .text = text, .arrayElement = false, .occurrences = 1};
    const std::string json = nesso::cli::renderMatchJson(match, 1);
    const std::string expected = std::string(R"({"schema_version":1,"rank":1,"source":"a)") + "\uFFFD" +
                                 R"(","kind":"line","line":1,"score":1,"text":")";
    CHECK(json.find(expected) == 0);
    CHECK(json.find(R"(\"\\\b\f\n\r\t\u0001\u000eok )") != std::string::npos);
    CHECK(json.find("\u00E9") != std::string::npos);
    CHECK(json.find("\u20AC") != std::string::npos);
    CHECK(json.find("\U0001F600") != std::string::npos);
    CHECK(json.find("occurrences\":1") != std::string::npos);
    CHECK(json.find("schema_version\":2") == std::string::npos);
    std::size_t replacements = 0;
    for (std::size_t position = 0; (position = json.find("\uFFFD", position)) != std::string::npos;
         position += std::string("\uFFFD").size())
    {
        ++replacements;
    }
    CHECK(replacements == 13);

    const nesso::TextMatch empty{
        .source = "", .line = 1, .score = 0.0F, .text = "", .arrayElement = false, .occurrences = 1};
    CHECK(nesso::cli::renderMatchJson(empty, 1) ==
          R"({"schema_version":1,"rank":1,"source":"","kind":"line","line":1,"score":0,"text":"","occurrences":1})");
}

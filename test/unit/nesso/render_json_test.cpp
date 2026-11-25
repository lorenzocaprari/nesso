// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_all.hpp>
#include <nesso_cli/render_json.hpp>

#include <clocale>
#include <optional>
#include <string>

TEST_CASE("json rows keep schema version 2 and append occurrences", "[json]")
{
    const nesso::TextMatch first{
        .source = "app.log", .line = 2, .score = 0.5F, .text = "same", .array_element = false, .occurrences = 3};
    const nesso::TextMatch second{
        .source = "rows.json", .line = 4, .score = -0.25F, .text = "only", .array_element = true, .occurrences = 1};

    CHECK(
        nesso::cli::RenderMatchJson(first, 1) ==
        R"({"schema_version":2,"rank":1,"source":"app.log","kind":"line","line":2,"score":0.5,"text":"same","occurrences":3,"mode":"dense","dense":null,"lexical":null})");
    CHECK(
        nesso::cli::RenderMatchJson(second, 2) ==
        R"({"schema_version":2,"rank":2,"source":"rows.json","kind":"element","line":4,"score":-0.25,"text":"only","occurrences":1,"mode":"dense","dense":null,"lexical":null})");
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
        .source = "a\xFF", .line = 1, .score = 1.0F, .text = text, .array_element = false, .occurrences = 1};
    const std::string json = nesso::cli::RenderMatchJson(match, 1);
    const std::string expected = std::string(R"({"schema_version":2,"rank":1,"source":"a)") + "\uFFFD" +
                                 R"(","kind":"line","line":1,"score":1,"text":")";
    CHECK(json.find(expected) == 0);
    CHECK(json.contains(R"(\"\\\b\f\n\r\t\u0001\u000eok )"));
    CHECK(json.contains("\u00E9"));
    CHECK(json.contains("\u20AC"));
    CHECK(json.contains("\U0001F600"));
    CHECK(json.contains("occurrences\":1"));
    CHECK(json.contains("schema_version\":2"));
    std::size_t replacements = 0;
    for (std::size_t position = 0; (position = json.find("\uFFFD", position)) != std::string::npos;
         position += std::string("\uFFFD").size())
    {
        ++replacements;
    }
    CHECK(replacements == 13);

    const nesso::TextMatch empty{
        .source = "", .line = 1, .score = 0.0F, .text = "", .array_element = false, .occurrences = 1};
    CHECK(
        nesso::cli::RenderMatchJson(empty, 1) ==
        R"({"schema_version":2,"rank":1,"source":"","kind":"line","line":1,"score":0,"text":"","occurrences":1,"mode":"dense","dense":null,"lexical":null})");
}

TEST_CASE("json score is the shortest round-trip form in any locale", "[json]")
{
    const nesso::TextMatch tenth{
        .source = "a", .line = 1, .score = 0.1F, .text = "t", .array_element = false, .occurrences = 1};
    const std::string under_classic = nesso::cli::RenderMatchJson(tenth, 1);
    CHECK(
        under_classic ==
        R"({"schema_version":2,"rank":1,"source":"a","kind":"line","line":1,"score":0.1,"text":"t","occurrences":1,"mode":"dense","dense":null,"lexical":null})");
    CHECK(!under_classic.contains("0.10000000149011612"));

    const nesso::TextMatch whole{
        .source = "a", .line = 1, .score = 1.0F, .text = "t", .array_element = false, .occurrences = 1};
    const std::string whole_json = nesso::cli::RenderMatchJson(whole, 1);
    CHECK(whole_json.contains("\"score\":1,"));
    CHECK(!whole_json.contains("\"score\":1.0"));

    const char *saved_locale = std::setlocale(LC_ALL, nullptr);
    const std::string saved = saved_locale == nullptr ? "C" : saved_locale;
    REQUIRE(std::setlocale(LC_ALL, "C") != nullptr);
    CHECK(nesso::cli::RenderMatchJson(tenth, 1) == under_classic);
#ifdef _WIN32
    REQUIRE(std::setlocale(LC_ALL, ".UTF-8") != nullptr);
#else
    if (std::setlocale(LC_ALL, "C.UTF-8") == nullptr)
    {
        REQUIRE(std::setlocale(LC_ALL, "C.utf8") != nullptr);
    }
#endif
    CHECK(nesso::cli::RenderMatchJson(tenth, 1) == under_classic);
    REQUIRE(std::setlocale(LC_ALL, saved.c_str()) != nullptr);
}

TEST_CASE("a truncated multi-byte tail becomes one replacement character", "[json]")
{
    const nesso::TextMatch match{
        .source = "log", .line = 7, .score = 0.1F, .text = "\xE2\x82", .array_element = false, .occurrences = 1};
    const std::string json = nesso::cli::RenderMatchJson(match, 4);
    const std::string expected =
        std::string(R"({"schema_version":2,"rank":4,"source":"log","kind":"line","line":7,"score":0.1,"text":")") +
        "\uFFFD" + R"(","occurrences":1,"mode":"dense","dense":null,"lexical":null})";
    CHECK(json == expected);

    const nesso::TextMatch interrupted{
        .source = "log", .line = 8, .score = -0.5F, .text = "\xE2\x82!", .array_element = true, .occurrences = 2};
    const std::string interrupted_json = nesso::cli::RenderMatchJson(interrupted, 1);
    CHECK(interrupted_json.contains(std::string("\"text\":\"") + "\uFFFD" + "!\""));
    CHECK(interrupted_json.contains("\"kind\":\"element\""));
    std::size_t replacements = 0;
    for (std::size_t position = 0; (position = interrupted_json.find("\uFFFD", position)) != std::string::npos;
         position += std::string("\uFFFD").size())
    {
        ++replacements;
    }
    CHECK(replacements == 1);
}

TEST_CASE("json rows expose hybrid mode and raw scores", "[json]")
{
    const nesso::TextMatch hybrid{.source = "blk.log",
                                  .line = 3,
                                  .score = 1.25F,
                                  .text = "blk_-1608999687919862906",
                                  .array_element = false,
                                  .occurrences = 1,
                                  .mode = "hybrid",
                                  .dense = 0.42F,
                                  .lexical = 3.5F};
    CHECK(
        nesso::cli::RenderMatchJson(hybrid, 1) ==
        R"({"schema_version":2,"rank":1,"source":"blk.log","kind":"line","line":3,"score":1.25,"text":"blk_-1608999687919862906","occurrences":1,"mode":"hybrid","dense":0.42,"lexical":3.5})");
}

TEST_CASE("provisional json rows append final false and the refresh count", "[json][provisional]")
{
    const nesso::TextMatch dense{.source = "app.log",
                                 .line = 2,
                                 .score = 0.5F,
                                 .text = "same",
                                 .array_element = false,
                                 .occurrences = 3,
                                 .mode = "dense",
                                 .dense = 0.5F,
                                 .lexical = std::nullopt};
    CHECK(
        nesso::cli::RenderProvisionalMatchJson(dense, 1, 1) ==
        R"({"schema_version":2,"rank":1,"source":"app.log","kind":"line","line":2,"score":0.5,"text":"same","occurrences":3,"mode":"dense","dense":0.5,"lexical":null,"final":false,"refresh":1})");

    const nesso::TextMatch element{
        .source = "rows.json", .line = 4, .score = -0.25F, .text = "only", .array_element = true, .occurrences = 1};
    const std::string record = nesso::cli::RenderProvisionalMatchJson(element, 2, 17);
    const std::string final_record = nesso::cli::RenderMatchJson(element, 2);
    CHECK(record == final_record.substr(0, final_record.size() - 1) + R"(,"final":false,"refresh":17})");
    CHECK(!final_record.contains("\"final\""));
}

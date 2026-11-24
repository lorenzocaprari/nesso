// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_all.hpp>
#include <parser/log_chunker.hpp>

#include <filesystem>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <unistd.h>
#include <utility>
#include <vector>

namespace
{

void WriteBytes(const std::filesystem::path &path, std::string_view bytes)
{
    std::ofstream output(path, std::ios::binary);
    REQUIRE(output);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    REQUIRE(output);
}

} // namespace

TEST_CASE("Chunker visits skipped lines with source byte ranges", "[Chunker][parser][Unit]")
{
    const auto directory =
        std::filesystem::temp_directory_path() / ("nesso-chunker-records-" + std::to_string(getpid()));
    REQUIRE(std::filesystem::create_directory(directory));
    const auto path = directory / "records.log";
    WriteBytes(path, "one\r\n\r\nfour\nend");
    nesso::parser::ParseStats stats{};
    std::vector<nesso::parser::ParsedChunk> records;
    REQUIRE(nesso::parser::Chunker::ForEachRecord(path, 4096, &stats, "message",
                                                  [&records](nesso::parser::ParsedChunk record) -> void
                                                  { records.push_back(std::move(record)); }));
    REQUIRE(records.size() == 4);
    CHECK(records[0].text == "one");
    CHECK(records[0].byte_offset == 0);
    CHECK(records[0].byte_length == 4);
    CHECK(records[1].skipped);
    CHECK(records[1].byte_offset == 5);
    CHECK(records[1].byte_length == 1);
    CHECK(records[2].byte_offset == 7);
    CHECK(records[2].byte_length == 4);
    CHECK(records[3].byte_offset == 12);
    CHECK(records[3].byte_length == 3);
    CHECK(stats.empty_lines == 1);

    const auto first = nesso::parser::Chunker::ReadRecord(
        path, {.text = {}, .line_number = 1, .byte_offset = 0, .array_element = false, .byte_length = 4}, "message",
        4096);
    REQUIRE(first);
    CHECK(*first == "one");
    const auto cut = nesso::parser::Chunker::ReadRecord(
        path, {.text = {}, .line_number = 3, .byte_offset = 7, .array_element = false, .byte_length = 4}, "message", 2);
    REQUIRE(cut);
    CHECK(*cut == "fo");
    CHECK_FALSE(nesso::parser::Chunker::ReadRecord(
        path, {.text = {}, .line_number = 1, .byte_offset = 100, .array_element = false, .byte_length = 4}, "message",
        4096));
    CHECK_FALSE(nesso::parser::Chunker::ReadRecord(
        path, {.text = {}, .line_number = 1, .byte_offset = 0, .array_element = true, .byte_length = 4}, "message",
        4096));
    CHECK_FALSE(nesso::parser::Chunker::ReadRecord(
        directory / "missing",
        {.text = {}, .line_number = 1, .byte_offset = 0, .array_element = false, .byte_length = 1}, "message", 4096));
    const auto unreadable = nesso::parser::Chunker::ForEachRecord(directory, 4096, nullptr, "message",
                                                                  [](nesso::parser::ParsedChunk) -> void {});
    REQUIRE_FALSE(unreadable);
    CHECK(unreadable.error() == nesso::parser::ParseError::FileOpenFailure);
    std::filesystem::remove_all(directory);
}

TEST_CASE("Blank records retain their source ranges and array ordinals", "[Chunker][parser][Unit]")
{
    const auto directory =
        std::filesystem::temp_directory_path() / ("nesso-chunker-blank-records-" + std::to_string(getpid()));
    REQUIRE(std::filesystem::create_directory(directory));
    const auto log = directory / "records.log";
    const std::string blank = " \t\r\n";
    const std::string kept = "  keep \t\r\n";
    const std::string tail = "\t ";
    WriteBytes(log, blank + kept + tail);

    nesso::parser::ParseStats stats{};
    std::vector<nesso::parser::ParsedChunk> records;
    REQUIRE(nesso::parser::Chunker::ForEachRecord(log, 4096, &stats, "message",
                                                  [&records](nesso::parser::ParsedChunk record) -> void
                                                  { records.push_back(std::move(record)); }));
    REQUIRE(records.size() == 3);
    CHECK(records[0].skipped);
    CHECK(records[0].line_number == 1);
    CHECK(records[0].byte_offset == 0);
    CHECK(records[0].byte_length == blank.size() - 1);
    CHECK_FALSE(records[1].skipped);
    CHECK(records[1].text == "  keep \t");
    CHECK(records[1].line_number == 2);
    CHECK(records[1].byte_offset == blank.size());
    CHECK(records[1].byte_length == kept.size() - 1);
    CHECK(records[2].skipped);
    CHECK(records[2].line_number == 3);
    CHECK(records[2].byte_offset == blank.size() + kept.size());
    CHECK(records[2].byte_length == tail.size());
    CHECK(stats.skipped_lines == 2);
    CHECK(stats.empty_lines == 2);
    CHECK(stats.truncated_lines == 0);
    const auto reread = nesso::parser::Chunker::ReadRecord(log,
                                                           {.text = {},
                                                            .line_number = records[1].line_number,
                                                            .byte_offset = records[1].byte_offset,
                                                            .array_element = false,
                                                            .byte_length = records[1].byte_length},
                                                           "message", 4096);
    REQUIRE(reread);
    CHECK(*reread == "  keep \t");

    const auto jsonl = directory / "records.jsonl";
    const std::string blank_value = "{\"msg\":\" \\t\"}\r\n";
    const std::string kept_value = "{\"msg\":\"keep\"}\n";
    WriteBytes(jsonl, blank + blank_value + kept_value);
    nesso::parser::ParseStats jsonl_stats{};
    std::vector<nesso::parser::ParsedChunk> jsonl_records;
    REQUIRE(nesso::parser::Chunker::ForEachRecord(jsonl, 4096, &jsonl_stats, "msg",
                                                  [&jsonl_records](nesso::parser::ParsedChunk record) -> void
                                                  { jsonl_records.push_back(std::move(record)); }));
    REQUIRE(jsonl_records.size() == 3);
    CHECK(jsonl_records[0].skipped);
    CHECK(jsonl_records[0].byte_offset == 0);
    CHECK(jsonl_records[0].byte_length == blank.size() - 1);
    CHECK(jsonl_records[1].skipped);
    CHECK(jsonl_records[1].line_number == 2);
    CHECK(jsonl_records[1].byte_offset == blank.size());
    CHECK(jsonl_records[1].byte_length == blank_value.size() - 1);
    CHECK_FALSE(jsonl_records[2].skipped);
    CHECK(jsonl_records[2].line_number == 3);
    CHECK(jsonl_records[2].byte_offset == blank.size() + blank_value.size());
    CHECK(jsonl_records[2].text == "keep");
    CHECK(jsonl_stats.skipped_lines == 2);
    CHECK(jsonl_stats.empty_lines == 2);
    CHECK(jsonl_stats.malformed_lines == 0);

    const auto json = directory / "records.json";
    WriteBytes(json, "[{\"msg\":\"\"},{\"msg\":\" \\t\"},{\"msg\":\"keep\"}]");
    nesso::parser::ParseStats json_stats{};
    std::vector<nesso::parser::ParsedChunk> json_records;
    REQUIRE(nesso::parser::Chunker::ForEachRecord(json, 4096, &json_stats, "msg",
                                                  [&json_records](nesso::parser::ParsedChunk record) -> void
                                                  { json_records.push_back(std::move(record)); }));
    REQUIRE(json_records.size() == 3);
    CHECK(json_records[0].skipped);
    CHECK(json_records[0].array_element);
    CHECK(json_records[0].line_number == 1);
    CHECK(json_records[1].skipped);
    CHECK(json_records[1].array_element);
    CHECK(json_records[1].line_number == 2);
    CHECK_FALSE(json_records[2].skipped);
    CHECK(json_records[2].array_element);
    CHECK(json_records[2].line_number == 3);
    CHECK(json_records[2].text == "keep");
    CHECK(json_stats.skipped_lines == 2);
    CHECK(json_stats.empty_lines == 2);
    std::filesystem::remove_all(directory);
}

TEST_CASE("Chunker re-reads decoded JSON values by field and array ordinal", "[Chunker][parser][Unit]")
{
    const auto directory =
        std::filesystem::temp_directory_path() / ("nesso-chunker-json-records-" + std::to_string(getpid()));
    REQUIRE(std::filesystem::create_directory(directory));
    const auto jsonl = directory / "records.JSONL";
    WriteBytes(jsonl, "{\"msg\":\"line\\nvalue\"}\r\n{bad\n{\"message\":\"missing\"}\n\n");
    nesso::parser::ParseStats stats{};
    std::vector<nesso::parser::ParsedChunk> records;
    REQUIRE(nesso::parser::Chunker::ForEachRecord(jsonl, 4096, &stats, "msg",
                                                  [&records](nesso::parser::ParsedChunk record) -> void
                                                  { records.push_back(std::move(record)); }));
    REQUIRE(records.size() == 4);
    CHECK(records[0].text == "line\nvalue");
    CHECK(records[1].skipped);
    CHECK(records[2].skipped);
    CHECK(records[3].skipped);
    CHECK(stats.malformed_lines == 1);
    CHECK(stats.missing_field_lines == 1);
    CHECK(stats.empty_lines == 1);
    size_t callbacks = 0;
    nesso::parser::ParseStats callback_stats{};
    CHECK_THROWS_AS(nesso::parser::Chunker::ForEachRecord(jsonl, 4096, &callback_stats, "msg",
                                                          [&callbacks](nesso::parser::ParsedChunk) -> void
                                                          {
                                                              ++callbacks;
                                                              const auto invalid = nlohmann::json::parse("{bad");
                                                              static_cast<void>(invalid);
                                                          }),
                    nlohmann::json::parse_error);
    CHECK(callbacks == 1);
    CHECK(callback_stats.malformed_lines == 0);
    const auto decoded = nesso::parser::Chunker::ReadRecord(jsonl,
                                                            {.text = {},
                                                             .line_number = 1,
                                                             .byte_offset = records[0].byte_offset,
                                                             .array_element = false,
                                                             .byte_length = records[0].byte_length},
                                                            "msg", 4096);
    REQUIRE(decoded);
    CHECK(*decoded == "line\nvalue");
    const auto truncated = nesso::parser::Chunker::ReadRecord(jsonl,
                                                              {.text = {},
                                                               .line_number = 1,
                                                               .byte_offset = records[0].byte_offset,
                                                               .array_element = false,
                                                               .byte_length = records[0].byte_length},
                                                              "msg", 4);
    REQUIRE(truncated);
    CHECK(*truncated == "line");
    CHECK_FALSE(nesso::parser::Chunker::ReadRecord(jsonl,
                                                   {.text = {},
                                                    .line_number = 1,
                                                    .byte_offset = records[0].byte_offset,
                                                    .array_element = true,
                                                    .byte_length = records[0].byte_length},
                                                   "msg", 4096));
    CHECK_FALSE(nesso::parser::Chunker::ReadRecord(jsonl,
                                                   {.text = {},
                                                    .line_number = 2,
                                                    .byte_offset = records[1].byte_offset,
                                                    .array_element = false,
                                                    .byte_length = records[1].byte_length},
                                                   "msg", 4096));
    CHECK_FALSE(nesso::parser::Chunker::ReadRecord(jsonl,
                                                   {.text = {},
                                                    .line_number = 3,
                                                    .byte_offset = records[2].byte_offset,
                                                    .array_element = false,
                                                    .byte_length = records[2].byte_length},
                                                   "msg", 4096));
    CHECK_FALSE(nesso::parser::Chunker::ReadRecord(jsonl,
                                                   {.text = {},
                                                    .line_number = 1,
                                                    .byte_offset = 0,
                                                    .array_element = false,
                                                    .byte_length = std::numeric_limits<uint64_t>::max()},
                                                   "msg", 4096));

    const auto json = directory / "array.json";
    WriteBytes(json, "[{\"msg\":\"first\"},{\"missing\":1},{\"msg\":\"third\"}]");
    records.clear();
    REQUIRE(nesso::parser::Chunker::ForEachRecord(json, 4096, nullptr, "msg",
                                                  [&records](nesso::parser::ParsedChunk record) -> void
                                                  { records.push_back(std::move(record)); }));
    REQUIRE(records.size() == 3);
    CHECK(records[1].skipped);
    CHECK(records[1].array_element);
    CHECK(records[2].line_number == 3);
    const auto third = nesso::parser::Chunker::ReadRecord(
        json, {.text = {}, .line_number = 3, .byte_offset = 0, .array_element = true, .byte_length = 0}, "msg", 4096);
    REQUIRE(third);
    CHECK(*third == "third");
    CHECK_FALSE(nesso::parser::Chunker::ReadRecord(
        json, {.text = {}, .line_number = 2, .byte_offset = 0, .array_element = true, .byte_length = 0}, "msg", 4096));
    CHECK_FALSE(nesso::parser::Chunker::ReadRecord(
        json, {.text = {}, .line_number = 4, .byte_offset = 0, .array_element = true, .byte_length = 0}, "msg", 4096));
    CHECK_FALSE(nesso::parser::Chunker::ReadRecord(
        json, {.text = {}, .line_number = 1, .byte_offset = 0, .array_element = false, .byte_length = 0}, "msg", 4096));
    callbacks = 0;
    callback_stats = {};
    CHECK_THROWS_AS(nesso::parser::Chunker::ForEachRecord(json, 4096, &callback_stats, "msg",
                                                          [&callbacks](nesso::parser::ParsedChunk) -> void
                                                          {
                                                              ++callbacks;
                                                              const auto invalid = nlohmann::json::parse("{bad");
                                                              static_cast<void>(invalid);
                                                          }),
                    nlohmann::json::parse_error);
    CHECK(callbacks == 1);
    CHECK(callback_stats.malformed_lines == 0);
    WriteBytes(json, "{\"msg\":\"object\"}");
    const auto object = nesso::parser::Chunker::ReadRecord(
        json, {.text = {}, .line_number = 1, .byte_offset = 0, .array_element = false, .byte_length = 0}, "msg", 4096);
    REQUIRE(object);
    CHECK(*object == "object");
    CHECK_FALSE(nesso::parser::Chunker::ReadRecord(
        json, {.text = {}, .line_number = 1, .byte_offset = 1, .array_element = false, .byte_length = 0}, "msg", 4096));
    CHECK_FALSE(nesso::parser::Chunker::ReadRecord(
        json, {.text = {}, .line_number = 2, .byte_offset = 0, .array_element = false, .byte_length = 0}, "msg", 4096));
    CHECK_FALSE(nesso::parser::Chunker::ReadRecord(
        json, {.text = {}, .line_number = 1, .byte_offset = 0, .array_element = true, .byte_length = 0}, "msg", 4096));
    WriteBytes(json, "{invalid");
    records.clear();
    REQUIRE(nesso::parser::Chunker::ForEachRecord(json, 4096, nullptr, "msg",
                                                  [&records](nesso::parser::ParsedChunk record) -> void
                                                  { records.push_back(std::move(record)); }));
    REQUIRE(records.size() == 1);
    CHECK(records[0].skipped);
    CHECK_FALSE(nesso::parser::Chunker::ReadRecord(
        json, {.text = {}, .line_number = 1, .byte_offset = 0, .array_element = false, .byte_length = 0}, "msg", 4096));
    std::filesystem::remove_all(directory);
}

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

void writeBytes(const std::filesystem::path &path, std::string_view bytes)
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
    writeBytes(path, "one\r\n\r\nfour\nend");
    nesso::parser::ParseStats stats{};
    std::vector<nesso::parser::ParsedChunk> records;
    REQUIRE(nesso::parser::Chunker::forEachRecord(path, 4096, &stats, "message",
                                                  [&records](nesso::parser::ParsedChunk record)
                                                  { records.push_back(std::move(record)); }));
    REQUIRE(records.size() == 4);
    CHECK(records[0].text == "one");
    CHECK(records[0].byteOffset == 0);
    CHECK(records[0].byteLength == 4);
    CHECK(records[1].skipped);
    CHECK(records[1].byteOffset == 5);
    CHECK(records[1].byteLength == 1);
    CHECK(records[2].byteOffset == 7);
    CHECK(records[2].byteLength == 4);
    CHECK(records[3].byteOffset == 12);
    CHECK(records[3].byteLength == 3);
    CHECK(stats.emptyLines == 1);

    const auto first = nesso::parser::Chunker::readRecord(path, 0, 4, 1, false, "message", 4096);
    REQUIRE(first);
    CHECK(*first == "one");
    const auto cut = nesso::parser::Chunker::readRecord(path, 7, 4, 3, false, "message", 2);
    REQUIRE(cut);
    CHECK(*cut == "fo");
    CHECK_FALSE(nesso::parser::Chunker::readRecord(path, 100, 4, 1, false, "message", 4096));
    CHECK_FALSE(nesso::parser::Chunker::readRecord(path, 0, 4, 1, true, "message", 4096));
    CHECK_FALSE(nesso::parser::Chunker::readRecord(directory / "missing", 0, 1, 1, false, "message", 4096));
    const auto unreadable =
        nesso::parser::Chunker::forEachRecord(directory, 4096, nullptr, "message", [](nesso::parser::ParsedChunk) {});
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
    writeBytes(log, blank + kept + tail);

    nesso::parser::ParseStats stats{};
    std::vector<nesso::parser::ParsedChunk> records;
    const auto collect = [&records](nesso::parser::ParsedChunk record) { records.push_back(std::move(record)); };
    REQUIRE(nesso::parser::Chunker::forEachRecord(log, 4096, &stats, "message", collect));
    REQUIRE(records.size() == 3);
    CHECK(records[0].skipped);
    CHECK(records[0].lineNumber == 1);
    CHECK(records[0].byteOffset == 0);
    CHECK(records[0].byteLength == blank.size() - 1);
    CHECK_FALSE(records[1].skipped);
    CHECK(records[1].text == "  keep \t");
    CHECK(records[1].lineNumber == 2);
    CHECK(records[1].byteOffset == blank.size());
    CHECK(records[1].byteLength == kept.size() - 1);
    CHECK(records[2].skipped);
    CHECK(records[2].lineNumber == 3);
    CHECK(records[2].byteOffset == blank.size() + kept.size());
    CHECK(records[2].byteLength == tail.size());
    CHECK(stats.skippedLines == 2);
    CHECK(stats.emptyLines == 2);
    CHECK(stats.truncatedLines == 0);
    const auto reread = nesso::parser::Chunker::readRecord(log, records[1].byteOffset, records[1].byteLength,
                                                           records[1].lineNumber, false, "message", 4096);
    REQUIRE(reread);
    CHECK(*reread == "  keep \t");

    const auto jsonl = directory / "records.jsonl";
    const std::string blankValue = "{\"msg\":\" \\t\"}\r\n";
    const std::string keptValue = "{\"msg\":\"keep\"}\n";
    writeBytes(jsonl, blank + blankValue + keptValue);
    records.clear();
    stats = {};
    REQUIRE(nesso::parser::Chunker::forEachRecord(jsonl, 4096, &stats, "msg", collect));
    REQUIRE(records.size() == 3);
    CHECK(records[0].skipped);
    CHECK(records[0].byteOffset == 0);
    CHECK(records[0].byteLength == blank.size() - 1);
    CHECK(records[1].skipped);
    CHECK(records[1].lineNumber == 2);
    CHECK(records[1].byteOffset == blank.size());
    CHECK(records[1].byteLength == blankValue.size() - 1);
    CHECK_FALSE(records[2].skipped);
    CHECK(records[2].lineNumber == 3);
    CHECK(records[2].byteOffset == blank.size() + blankValue.size());
    CHECK(records[2].text == "keep");
    CHECK(stats.skippedLines == 2);
    CHECK(stats.emptyLines == 2);
    CHECK(stats.malformedLines == 0);

    const auto json = directory / "records.json";
    writeBytes(json, "[{\"msg\":\"\"},{\"msg\":\" \\t\"},{\"msg\":\"keep\"}]");
    records.clear();
    stats = {};
    REQUIRE(nesso::parser::Chunker::forEachRecord(json, 4096, &stats, "msg", collect));
    REQUIRE(records.size() == 3);
    CHECK(records[0].skipped);
    CHECK(records[0].arrayElement);
    CHECK(records[0].lineNumber == 1);
    CHECK(records[1].skipped);
    CHECK(records[1].arrayElement);
    CHECK(records[1].lineNumber == 2);
    CHECK_FALSE(records[2].skipped);
    CHECK(records[2].arrayElement);
    CHECK(records[2].lineNumber == 3);
    CHECK(records[2].text == "keep");
    CHECK(stats.skippedLines == 2);
    CHECK(stats.emptyLines == 2);
    std::filesystem::remove_all(directory);
}

TEST_CASE("Chunker re-reads decoded JSON values by field and array ordinal", "[Chunker][parser][Unit]")
{
    const auto directory =
        std::filesystem::temp_directory_path() / ("nesso-chunker-json-records-" + std::to_string(getpid()));
    REQUIRE(std::filesystem::create_directory(directory));
    const auto jsonl = directory / "records.JSONL";
    writeBytes(jsonl, "{\"msg\":\"line\\nvalue\"}\r\n{bad\n{\"message\":\"missing\"}\n\n");
    nesso::parser::ParseStats stats{};
    std::vector<nesso::parser::ParsedChunk> records;
    REQUIRE(nesso::parser::Chunker::forEachRecord(jsonl, 4096, &stats, "msg",
                                                  [&records](nesso::parser::ParsedChunk record)
                                                  { records.push_back(std::move(record)); }));
    REQUIRE(records.size() == 4);
    CHECK(records[0].text == "line\nvalue");
    CHECK(records[1].skipped);
    CHECK(records[2].skipped);
    CHECK(records[3].skipped);
    CHECK(stats.malformedLines == 1);
    CHECK(stats.missingFieldLines == 1);
    CHECK(stats.emptyLines == 1);
    size_t callbacks = 0;
    nesso::parser::ParseStats callbackStats{};
    CHECK_THROWS_AS(nesso::parser::Chunker::forEachRecord(jsonl, 4096, &callbackStats, "msg",
                                                          [&callbacks](nesso::parser::ParsedChunk)
                                                          {
                                                              ++callbacks;
                                                              const auto invalid = nlohmann::json::parse("{bad");
                                                              static_cast<void>(invalid);
                                                          }),
                    nlohmann::json::parse_error);
    CHECK(callbacks == 1);
    CHECK(callbackStats.malformedLines == 0);
    const auto decoded =
        nesso::parser::Chunker::readRecord(jsonl, records[0].byteOffset, records[0].byteLength, 1, false, "msg", 4096);
    REQUIRE(decoded);
    CHECK(*decoded == "line\nvalue");
    const auto truncated =
        nesso::parser::Chunker::readRecord(jsonl, records[0].byteOffset, records[0].byteLength, 1, false, "msg", 4);
    REQUIRE(truncated);
    CHECK(*truncated == "line");
    CHECK_FALSE(
        nesso::parser::Chunker::readRecord(jsonl, records[0].byteOffset, records[0].byteLength, 1, true, "msg", 4096));
    CHECK_FALSE(
        nesso::parser::Chunker::readRecord(jsonl, records[1].byteOffset, records[1].byteLength, 2, false, "msg", 4096));
    CHECK_FALSE(
        nesso::parser::Chunker::readRecord(jsonl, records[2].byteOffset, records[2].byteLength, 3, false, "msg", 4096));
    CHECK_FALSE(
        nesso::parser::Chunker::readRecord(jsonl, 0, std::numeric_limits<uint64_t>::max(), 1, false, "msg", 4096));

    const auto json = directory / "array.json";
    writeBytes(json, "[{\"msg\":\"first\"},{\"missing\":1},{\"msg\":\"third\"}]");
    records.clear();
    REQUIRE(nesso::parser::Chunker::forEachRecord(json, 4096, nullptr, "msg",
                                                  [&records](nesso::parser::ParsedChunk record)
                                                  { records.push_back(std::move(record)); }));
    REQUIRE(records.size() == 3);
    CHECK(records[1].skipped);
    CHECK(records[1].arrayElement);
    CHECK(records[2].lineNumber == 3);
    const auto third = nesso::parser::Chunker::readRecord(json, 0, 0, 3, true, "msg", 4096);
    REQUIRE(third);
    CHECK(*third == "third");
    CHECK_FALSE(nesso::parser::Chunker::readRecord(json, 0, 0, 2, true, "msg", 4096));
    CHECK_FALSE(nesso::parser::Chunker::readRecord(json, 0, 0, 4, true, "msg", 4096));
    CHECK_FALSE(nesso::parser::Chunker::readRecord(json, 0, 0, 1, false, "msg", 4096));
    callbacks = 0;
    callbackStats = {};
    CHECK_THROWS_AS(nesso::parser::Chunker::forEachRecord(json, 4096, &callbackStats, "msg",
                                                          [&callbacks](nesso::parser::ParsedChunk)
                                                          {
                                                              ++callbacks;
                                                              const auto invalid = nlohmann::json::parse("{bad");
                                                              static_cast<void>(invalid);
                                                          }),
                    nlohmann::json::parse_error);
    CHECK(callbacks == 1);
    CHECK(callbackStats.malformedLines == 0);
    writeBytes(json, "{\"msg\":\"object\"}");
    const auto object = nesso::parser::Chunker::readRecord(json, 0, 0, 1, false, "msg", 4096);
    REQUIRE(object);
    CHECK(*object == "object");
    CHECK_FALSE(nesso::parser::Chunker::readRecord(json, 1, 0, 1, false, "msg", 4096));
    CHECK_FALSE(nesso::parser::Chunker::readRecord(json, 0, 0, 2, false, "msg", 4096));
    CHECK_FALSE(nesso::parser::Chunker::readRecord(json, 0, 0, 1, true, "msg", 4096));
    writeBytes(json, "{invalid");
    records.clear();
    REQUIRE(nesso::parser::Chunker::forEachRecord(json, 4096, nullptr, "msg",
                                                  [&records](nesso::parser::ParsedChunk record)
                                                  { records.push_back(std::move(record)); }));
    REQUIRE(records.size() == 1);
    CHECK(records[0].skipped);
    CHECK_FALSE(nesso::parser::Chunker::readRecord(json, 0, 0, 1, false, "msg", 4096));
    std::filesystem::remove_all(directory);
}

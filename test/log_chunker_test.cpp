#include <catch2/catch_all.hpp>
#include <parser/log_chunker.hpp>

#include <filesystem>

#ifndef NESSO_TEST_FIXTURES
#error "NESSO_TEST_FIXTURES must be defined"
#endif

TEST_CASE("LogChunker parses .log files line-by-line", "[LogChunker][parser][Unit]")
{
    const std::filesystem::path path = std::filesystem::path(NESSO_TEST_FIXTURES) / "sample.log";
    parser::ParseStats stats{};
    const auto chunks = parser::LogChunker::fromLogFile(path, 64, &stats);
    REQUIRE(chunks.has_value());
    REQUIRE(chunks->size() == 2);
    REQUIRE((*chunks)[0].text == "alpha line");
    REQUIRE((*chunks)[0].lineNumber == 1);
    REQUIRE((*chunks)[1].text == "gamma line");
    REQUIRE(stats.skippedLines >= 2);
}

TEST_CASE("LogChunker extracts message fields from jsonl", "[LogChunker][parser][Unit]")
{
    const std::filesystem::path path = std::filesystem::path(NESSO_TEST_FIXTURES) / "sample.jsonl";
    parser::ParseStats stats{};
    const auto chunks = parser::LogChunker::fromJsonFile(path, &stats);
    REQUIRE(chunks.has_value());
    REQUIRE(chunks->size() == 2);
    REQUIRE((*chunks)[0].text == "payment timeout after 30s");
    REQUIRE((*chunks)[1].text == "database connection refused");
    REQUIRE(stats.skippedLines == 1);
}

TEST_CASE("LogChunker routes fromFile by extension", "[LogChunker][parser][Unit]")
{
    const auto logChunks = parser::LogChunker::fromFile(std::filesystem::path(NESSO_TEST_FIXTURES) / "sample.log");
    REQUIRE(logChunks.has_value());
    REQUIRE_FALSE(logChunks->empty());

    const auto jsonlChunks = parser::LogChunker::fromFile(std::filesystem::path(NESSO_TEST_FIXTURES) / "sample.jsonl");
    REQUIRE(jsonlChunks.has_value());
    REQUIRE(jsonlChunks->size() == 2);
}

TEST_CASE("LogChunker parses .json documents", "[LogChunker][parser][Unit]")
{
    const auto single = parser::LogChunker::fromFile(std::filesystem::path(NESSO_TEST_FIXTURES) / "sample.json");
    REQUIRE(single.has_value());
    REQUIRE(single->size() == 1);
    REQUIRE((*single)[0].text == "standalone entry");

    const auto array = parser::LogChunker::fromFile(std::filesystem::path(NESSO_TEST_FIXTURES) / "sample_array.json");
    REQUIRE(array.has_value());
    REQUIRE(array->size() == 2);
    REQUIRE((*array)[0].text == "first entry");
    REQUIRE((*array)[1].text == "second entry");
}

TEST_CASE("LogChunker skips malformed jsonl lines", "[LogChunker][parser][Unit]")
{
    parser::ParseStats stats{};
    const auto chunks =
        parser::LogChunker::fromJsonFile(std::filesystem::path(NESSO_TEST_FIXTURES) / "sample_malformed.jsonl", &stats);
    REQUIRE(chunks.has_value());
    REQUIRE(chunks->size() == 2);
    REQUIRE((*chunks)[0].text == "valid line");
    REQUIRE((*chunks)[1].text == "after bad line");
    REQUIRE(stats.skippedLines == 1);
}

TEST_CASE("LogChunker tolerates invalid json documents", "[LogChunker][parser][Unit]")
{
    parser::ParseStats stats{};
    const auto chunks =
        parser::LogChunker::fromJsonFile(std::filesystem::path(NESSO_TEST_FIXTURES) / "sample_invalid.json", &stats);
    REQUIRE(chunks.has_value());
    REQUIRE(chunks->empty());
    REQUIRE(stats.skippedLines == 1);
}

TEST_CASE("LogChunker reports file open failures", "[LogChunker][parser][Unit]")
{
    const auto logChunks = parser::LogChunker::fromLogFile("missing-sample.log");
    REQUIRE_FALSE(logChunks.has_value());
    REQUIRE(logChunks.error() == parser::ParseError::FileOpenFailure);

    const auto jsonChunks = parser::LogChunker::fromJsonFile("missing-sample.json");
    REQUIRE_FALSE(jsonChunks.has_value());
    REQUIRE(jsonChunks.error() == parser::ParseError::FileOpenFailure);
}

TEST_CASE("LogChunker rejects unsupported extensions", "[LogChunker][parser][Unit]")
{
    const auto chunks = parser::LogChunker::fromFile("README.md");
    REQUIRE_FALSE(chunks.has_value());
    REQUIRE(chunks.error() == parser::ParseError::UnsupportedFormat);
}

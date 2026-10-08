#include <catch2/catch_all.hpp>
#include <parser/log_chunker.hpp>

#include <filesystem>
#include <fstream>
#include <string_view>

namespace
{

void writeBytes(const std::filesystem::path &path, std::string_view bytes)
{
    std::ofstream output(path, std::ios::binary);
    REQUIRE(output);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

} // namespace

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
    REQUIRE((*chunks)[0].byteOffset == 0);
    REQUIRE((*chunks)[1].text == "gamma line");
    REQUIRE((*chunks)[1].byteOffset == 209);
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
    REQUIRE((*chunks)[0].byteOffset == 0);
    REQUIRE((*chunks)[1].text == "database connection refused");
    REQUIRE((*chunks)[1].byteOffset == 66);
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

    parser::ParseStats stats{};
    const auto skipped =
        parser::LogChunker::fromFile(std::filesystem::path(NESSO_TEST_FIXTURES) / "sample.log", 64, &stats);
    REQUIRE(skipped.has_value());
    REQUIRE(skipped->size() == 2);
    REQUIRE(stats.skippedLines >= 2);
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

TEST_CASE("LogChunker keeps a final line that has no trailing newline", "[LogChunker][parser][Unit]")
{
    const auto directory = std::filesystem::temp_directory_path() / "nesso-chunker-eof";
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);

    const auto logPath = directory / "tail.log";
    writeBytes(logPath, "alpha\nbeta");
    const auto logChunks = parser::LogChunker::fromLogFile(logPath);
    REQUIRE(logChunks.has_value());
    REQUIRE(logChunks->size() == 2);
    REQUIRE((*logChunks)[1].text == "beta");
    REQUIRE((*logChunks)[1].byteOffset == 6);

    const auto jsonlPath = directory / "tail.jsonl";
    writeBytes(jsonlPath, "{\"message\":\"only\"}");
    const auto jsonlChunks = parser::LogChunker::fromJsonFile(jsonlPath);
    REQUIRE(jsonlChunks.has_value());
    REQUIRE(jsonlChunks->size() == 1);
    REQUIRE((*jsonlChunks)[0].text == "only");

    std::filesystem::remove_all(directory);
}

TEST_CASE("LogChunker skips blank jsonl lines and non-string messages", "[LogChunker][parser][Unit]")
{
    const auto directory = std::filesystem::temp_directory_path() / "nesso-chunker-skip";
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);

    const auto blank = directory / "blank.jsonl";
    writeBytes(blank, "{\"message\":\"one\"}\n\n{\"message\":\"two\"}\n");

    parser::ParseStats stats{};
    const auto counted = parser::LogChunker::fromJsonFile(blank, &stats);
    REQUIRE(counted.has_value());
    REQUIRE(counted->size() == 2);
    REQUIRE(stats.skippedLines == 1);

    const auto uncounted = parser::LogChunker::fromFile(blank);
    REQUIRE(uncounted.has_value());
    REQUIRE(uncounted->size() == 2);

    const auto numeric = directory / "numeric.jsonl";
    writeBytes(numeric, "{\"message\":1}\n{\"message\":\"kept\"}\n");
    parser::ParseStats numericStats{};
    const auto numericChunks = parser::LogChunker::fromJsonFile(numeric, &numericStats);
    REQUIRE(numericChunks.has_value());
    REQUIRE(numericChunks->size() == 1);
    REQUIRE((*numericChunks)[0].text == "kept");
    REQUIRE(numericStats.skippedLines == 1);

    std::filesystem::remove_all(directory);
}

TEST_CASE("LogChunker skips invalid documents when stats are not collected", "[LogChunker][parser][Unit]")
{
    const auto directory = std::filesystem::temp_directory_path() / "nesso-chunker-invalid";
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);

    const auto jsonl = directory / "bad.jsonl";
    writeBytes(jsonl, "{not valid\n{\"message\":\"kept\"}\n");
    const auto jsonlChunks = parser::LogChunker::fromFile(jsonl);
    REQUIRE(jsonlChunks.has_value());
    REQUIRE(jsonlChunks->size() == 1);
    REQUIRE((*jsonlChunks)[0].text == "kept");

    const auto json = directory / "bad.json";
    writeBytes(json, "{not valid");
    const auto jsonChunks = parser::LogChunker::fromFile(json);
    REQUIRE(jsonChunks.has_value());
    REQUIRE(jsonChunks->empty());

    std::filesystem::remove_all(directory);
}

#include <catch2/catch_all.hpp>
#include <parser/log_chunker.hpp>

#include <filesystem>
#include <fstream>
#include <string>
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

TEST_CASE("Chunker parses .log files line-by-line", "[Chunker][parser][Unit]")
{
    const std::filesystem::path path = std::filesystem::path(NESSO_TEST_FIXTURES) / "sample.log";
    nesso::parser::ParseStats stats{};
    const auto chunks = nesso::parser::Chunker::fromLogFile(path, 64, &stats);
    REQUIRE(chunks.has_value());
    REQUIRE(chunks->size() == 3);
    REQUIRE((*chunks)[0].text == "alpha line");
    REQUIRE((*chunks)[0].lineNumber == 1);
    REQUIRE((*chunks)[0].byteOffset == 0);
    REQUIRE((*chunks)[1].text == "beta line that is intentionally very long and should be skipped ");
    REQUIRE((*chunks)[1].text.size() == 64);
    REQUIRE((*chunks)[2].text == "gamma line");
    REQUIRE((*chunks)[2].byteOffset == 209);
    REQUIRE(stats.skippedLines == 2);
    REQUIRE(stats.emptyLines == 2);
    REQUIRE(stats.truncatedLines == 1);
}

TEST_CASE("Chunker extracts message fields from jsonl", "[Chunker][parser][Unit]")
{
    const std::filesystem::path path = std::filesystem::path(NESSO_TEST_FIXTURES) / "sample.jsonl";
    nesso::parser::ParseStats stats{};
    const auto chunks = nesso::parser::Chunker::fromJsonFile(path, &stats);
    REQUIRE(chunks.has_value());
    REQUIRE(chunks->size() == 2);
    REQUIRE((*chunks)[0].text == "payment timeout after 30s");
    REQUIRE((*chunks)[0].byteOffset == 0);
    REQUIRE((*chunks)[1].text == "database connection refused");
    REQUIRE((*chunks)[1].byteOffset == 66);
    REQUIRE(stats.skippedLines == 1);
    REQUIRE(stats.missingFieldLines == 1);
}

TEST_CASE("Chunker routes fromFile by extension", "[Chunker][parser][Unit]")
{
    const auto logChunks = nesso::parser::Chunker::fromFile(std::filesystem::path(NESSO_TEST_FIXTURES) / "sample.log");
    REQUIRE(logChunks.has_value());
    REQUIRE_FALSE(logChunks->empty());

    const auto jsonlChunks =
        nesso::parser::Chunker::fromFile(std::filesystem::path(NESSO_TEST_FIXTURES) / "sample.jsonl");
    REQUIRE(jsonlChunks.has_value());
    REQUIRE(jsonlChunks->size() == 2);

    nesso::parser::ParseStats stats{};
    const auto limited =
        nesso::parser::Chunker::fromFile(std::filesystem::path(NESSO_TEST_FIXTURES) / "sample.log", 64, &stats);
    REQUIRE(limited.has_value());
    REQUIRE(limited->size() == 3);
    REQUIRE(stats.skippedLines == 2);
    REQUIRE(stats.truncatedLines == 1);
}

TEST_CASE("Chunker parses .json documents", "[Chunker][parser][Unit]")
{
    const auto single = nesso::parser::Chunker::fromFile(std::filesystem::path(NESSO_TEST_FIXTURES) / "sample.json");
    REQUIRE(single.has_value());
    REQUIRE(single->size() == 1);
    REQUIRE((*single)[0].text == "standalone entry");

    const auto array =
        nesso::parser::Chunker::fromFile(std::filesystem::path(NESSO_TEST_FIXTURES) / "sample_array.json");
    REQUIRE(array.has_value());
    REQUIRE(array->size() == 2);
    REQUIRE((*array)[0].text == "first entry");
    REQUIRE((*array)[1].text == "second entry");
}

TEST_CASE("Chunker skips malformed jsonl lines", "[Chunker][parser][Unit]")
{
    nesso::parser::ParseStats stats{};
    const auto chunks = nesso::parser::Chunker::fromJsonFile(
        std::filesystem::path(NESSO_TEST_FIXTURES) / "sample_malformed.jsonl", &stats);
    REQUIRE(chunks.has_value());
    REQUIRE(chunks->size() == 2);
    REQUIRE((*chunks)[0].text == "valid line");
    REQUIRE((*chunks)[1].text == "after bad line");
    REQUIRE(stats.skippedLines == 1);
    REQUIRE(stats.malformedLines == 1);
}

TEST_CASE("Chunker tolerates invalid json documents", "[Chunker][parser][Unit]")
{
    nesso::parser::ParseStats stats{};
    const auto chunks = nesso::parser::Chunker::fromJsonFile(
        std::filesystem::path(NESSO_TEST_FIXTURES) / "sample_invalid.json", &stats);
    REQUIRE(chunks.has_value());
    REQUIRE(chunks->empty());
    REQUIRE(stats.skippedLines == 1);
    REQUIRE(stats.malformedLines == 1);
}

TEST_CASE("Chunker reports file open failures", "[Chunker][parser][Unit]")
{
    const auto logChunks = nesso::parser::Chunker::fromLogFile("missing-sample.log");
    REQUIRE_FALSE(logChunks.has_value());
    REQUIRE(logChunks.error() == nesso::parser::ParseError::FileOpenFailure);

    const auto jsonChunks = nesso::parser::Chunker::fromJsonFile("missing-sample.json");
    REQUIRE_FALSE(jsonChunks.has_value());
    REQUIRE(jsonChunks.error() == nesso::parser::ParseError::FileOpenFailure);
}

TEST_CASE("Chunker reads any non-JSON name as lines", "[Chunker][parser][Unit]")
{
    const auto directory = std::filesystem::temp_directory_path() / "nesso-chunker-names";
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);

    const auto expectLine = [&](const char *name)
    {
        const auto path = directory / name;
        writeBytes(path, "database connection refused\n");
        const auto chunks = nesso::parser::Chunker::fromFile(path);
        REQUIRE(chunks.has_value());
        REQUIRE(chunks->size() == 1);
        REQUIRE((*chunks)[0].text == "database connection refused");
        REQUIRE_FALSE((*chunks)[0].arrayElement);
    };
    expectLine("syslog");
    expectLine("APP.LOG");
    expectLine("app.log.1");
    expectLine("notes.txt");

    const auto jsonPath = directory / "rows.JSON";
    writeBytes(jsonPath, "{\"message\":\"standalone entry\"}\n");
    const auto jsonChunks = nesso::parser::Chunker::fromFile(jsonPath);
    REQUIRE(jsonChunks.has_value());
    REQUIRE(jsonChunks->size() == 1);
    REQUIRE((*jsonChunks)[0].text == "standalone entry");

    const auto jsonlPath = directory / "rows.JSONL";
    writeBytes(jsonlPath, "{\"message\":\"one\"}\n{\"message\":\"two\"}\n");
    const auto jsonlChunks = nesso::parser::Chunker::fromFile(jsonlPath);
    REQUIRE(jsonlChunks.has_value());
    REQUIRE(jsonlChunks->size() == 2);

    std::filesystem::remove_all(directory);
}

TEST_CASE("Chunker rejects a NUL in the first 4 KiB", "[Chunker][parser][Unit]")
{
    const auto directory = std::filesystem::temp_directory_path() / "nesso-chunker-binary";
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);

    const auto binary = directory / "blob.log";
    writeBytes(binary, std::string("hello\0world", 11));
    const auto rejected = nesso::parser::Chunker::fromFile(binary);
    REQUIRE_FALSE(rejected.has_value());
    REQUIRE(rejected.error() == nesso::parser::ParseError::BinaryFile);

    std::string late(4096, 'a');
    late.push_back('\0');
    late += "tail\n";
    const auto after = directory / "late.log";
    writeBytes(after, late);
    const auto kept = nesso::parser::Chunker::fromFile(after);
    REQUIRE(kept.has_value());
    REQUIRE_FALSE(kept->empty());

    std::filesystem::remove_all(directory);
}

TEST_CASE("Chunker numbers JSON array elements", "[Chunker][parser][Unit]")
{
    const auto array =
        nesso::parser::Chunker::fromFile(std::filesystem::path(NESSO_TEST_FIXTURES) / "sample_array.json");
    REQUIRE(array.has_value());
    REQUIRE(array->size() == 2);
    REQUIRE((*array)[0].arrayElement);
    REQUIRE((*array)[0].lineNumber == 1);
    REQUIRE((*array)[1].arrayElement);
    REQUIRE((*array)[1].lineNumber == 3);
}

TEST_CASE("Chunker keeps a final line that has no trailing newline", "[Chunker][parser][Unit]")
{
    const auto directory = std::filesystem::temp_directory_path() / "nesso-chunker-eof";
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);

    const auto logPath = directory / "tail.log";
    writeBytes(logPath, "alpha\nbeta");
    const auto logChunks = nesso::parser::Chunker::fromLogFile(logPath);
    REQUIRE(logChunks.has_value());
    REQUIRE(logChunks->size() == 2);
    REQUIRE((*logChunks)[1].text == "beta");
    REQUIRE((*logChunks)[1].byteOffset == 6);

    const auto jsonlPath = directory / "tail.jsonl";
    writeBytes(jsonlPath, "{\"message\":\"only\"}");
    const auto jsonlChunks = nesso::parser::Chunker::fromJsonFile(jsonlPath);
    REQUIRE(jsonlChunks.has_value());
    REQUIRE(jsonlChunks->size() == 1);
    REQUIRE((*jsonlChunks)[0].text == "only");

    std::filesystem::remove_all(directory);
}

TEST_CASE("Chunker skips blank jsonl lines and non-string messages", "[Chunker][parser][Unit]")
{
    const auto directory = std::filesystem::temp_directory_path() / "nesso-chunker-skip";
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);

    const auto blank = directory / "blank.jsonl";
    writeBytes(blank, "{\"message\":\"one\"}\n\n{\"message\":\"two\"}\n");

    nesso::parser::ParseStats stats{};
    const auto counted = nesso::parser::Chunker::fromJsonFile(blank, &stats);
    REQUIRE(counted.has_value());
    REQUIRE(counted->size() == 2);
    REQUIRE(stats.skippedLines == 1);

    const auto uncounted = nesso::parser::Chunker::fromFile(blank);
    REQUIRE(uncounted.has_value());
    REQUIRE(uncounted->size() == 2);

    const auto numeric = directory / "numeric.jsonl";
    writeBytes(numeric, "{\"message\":1}\n{\"message\":\"kept\"}\n");
    nesso::parser::ParseStats numericStats{};
    const auto numericChunks = nesso::parser::Chunker::fromJsonFile(numeric, &numericStats);
    REQUIRE(numericChunks.has_value());
    REQUIRE(numericChunks->size() == 1);
    REQUIRE((*numericChunks)[0].text == "kept");
    REQUIRE(numericStats.skippedLines == 1);

    std::filesystem::remove_all(directory);
}

TEST_CASE("Chunker skips invalid documents when stats are not collected", "[Chunker][parser][Unit]")
{
    const auto directory = std::filesystem::temp_directory_path() / "nesso-chunker-invalid";
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);

    const auto jsonl = directory / "bad.jsonl";
    writeBytes(jsonl, "{not valid\n{\"message\":\"kept\"}\n");
    const auto jsonlChunks = nesso::parser::Chunker::fromFile(jsonl);
    REQUIRE(jsonlChunks.has_value());
    REQUIRE(jsonlChunks->size() == 1);
    REQUIRE((*jsonlChunks)[0].text == "kept");

    const auto json = directory / "bad.json";
    writeBytes(json, "{not valid");
    const auto jsonChunks = nesso::parser::Chunker::fromFile(json);
    REQUIRE(jsonChunks.has_value());
    REQUIRE(jsonChunks->empty());

    std::filesystem::remove_all(directory);
}

TEST_CASE("Chunker strips a carriage return and reads the selected JSON field", "[Chunker][parser][Unit]")
{
    const auto directory = std::filesystem::temp_directory_path() / "nesso-chunker-field";
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);

    const auto logPath = directory / "crlf.log";
    writeBytes(logPath, "hello\r\nworld\r\n");
    const auto logChunks = nesso::parser::Chunker::fromLogFile(logPath);
    REQUIRE(logChunks.has_value());
    REQUIRE(logChunks->size() == 2);
    REQUIRE((*logChunks)[0].text == "hello");
    REQUIRE((*logChunks)[1].text == "world");
    REQUIRE((*logChunks)[1].byteOffset == 7);

    const auto jsonlPath = directory / "fields.jsonl";
    writeBytes(jsonlPath, "{\"message\":\"ignored\",\"msg\":\"kept\"}\r\n{\"msg\":1}\n");
    nesso::parser::ParseStats stats{};
    const auto jsonlChunks = nesso::parser::Chunker::fromJsonFile(jsonlPath, &stats, "msg");
    REQUIRE(jsonlChunks.has_value());
    REQUIRE(jsonlChunks->size() == 1);
    REQUIRE((*jsonlChunks)[0].text == "kept");
    REQUIRE(stats.skippedLines == 1);

    const auto longValue = directory / "long.json";
    writeBytes(longValue, "{\"message\":\"" + std::string(80, 'a') + "\"}");
    nesso::parser::ParseStats longStats{};
    const auto longChunks = nesso::parser::Chunker::fromJsonFile(longValue, &longStats, "message", 8);
    REQUIRE(longChunks.has_value());
    REQUIRE(longChunks->size() == 1);
    REQUIRE((*longChunks)[0].text == "aaaaaaaa");
    REQUIRE(longStats.truncatedLines == 1);
    REQUIRE(longStats.skippedLines == 0);

    std::filesystem::remove_all(directory);
}

namespace
{

struct Limited
{
    std::string text;
    size_t truncated = 0;
};

Limited limitedLine(const std::string &line, size_t maxLineLength)
{
    const auto directory = std::filesystem::temp_directory_path() / "nesso-chunker-limit";
    std::filesystem::create_directories(directory);
    const auto path = directory / "line.log";
    writeBytes(path, line + "\n");
    nesso::parser::ParseStats stats{};
    const auto chunks = nesso::parser::Chunker::fromLogFile(path, maxLineLength, &stats);
    std::filesystem::remove_all(directory);
    REQUIRE(chunks.has_value());
    REQUIRE(chunks->size() == 1);
    return Limited{.text = (*chunks)[0].text, .truncated = stats.truncatedLines};
}

} // namespace

TEST_CASE("Chunker truncates at code points, not bytes", "[Chunker][parser][Unit]")
{
    SECTION("ASCII keeps the byte behaviour around the limit")
    {
        REQUIRE(limitedLine(std::string(4095, 'a'), 4096).truncated == 0);
        REQUIRE(limitedLine(std::string(4096, 'a'), 4096).truncated == 0);
        const auto cut = limitedLine(std::string(4097, 'a'), 4096);
        REQUIRE(cut.text == std::string(4096, 'a'));
        REQUIRE(cut.truncated == 1);
    }

    SECTION("multi-byte text up to the limit is kept whole")
    {
        std::string text;
        for (int index = 0; index < 4096; ++index)
        {
            text += "\xC3\xA9";
        }
        const auto kept = limitedLine(text, 4096);
        REQUIRE(kept.text == text);
        REQUIRE(kept.truncated == 0);
    }

    SECTION("a 2-, 3- or 4-byte character on the boundary is kept whole")
    {
        for (const std::string character : {"\xC3\xA9", "\xE2\x82\xAC", "\xF0\x9F\x98\x80"})
        {
            const std::string prefix(3, 'a');
            const auto cut = limitedLine(prefix + character + "zz", 4);
            REQUIRE(cut.text == prefix + character);
            REQUIRE(cut.truncated == 1);
        }
    }

    SECTION("a character after the limit is dropped whole")
    {
        const auto cut = limitedLine("abcd\xF0\x9F\x98\x80", 4);
        REQUIRE(cut.text == "abcd");
        REQUIRE(cut.truncated == 1);
    }

    SECTION("invalid bytes count one each")
    {
        const auto lone = limitedLine("\x80\x80\x80\x80\x80", 4);
        REQUIRE(lone.text == "\x80\x80\x80\x80");
        REQUIRE(lone.truncated == 1);

        const std::string partial = "a\xE2\x82";
        const auto broken = limitedLine(partial + "bcd", 4);
        REQUIRE(broken.text == partial + "b");
        REQUIRE(broken.truncated == 1);
    }

    SECTION("a sequence cut short by the end of the line counts byte by byte")
    {
        const auto cut = limitedLine("ab\xF0\x9F", 3);
        REQUIRE(cut.text == "ab\xF0");
        REQUIRE(cut.truncated == 1);
    }
}

TEST_CASE("Chunker truncates JSON values at code points", "[Chunker][parser][Unit]")
{
    const auto directory = std::filesystem::temp_directory_path() / "nesso-chunker-json-limit";
    std::filesystem::create_directories(directory);
    const auto path = directory / "values.jsonl";
    writeBytes(path, "{\"message\":\"abc\\u00e9\\u00e9\"}\n");
    nesso::parser::ParseStats stats{};
    const auto chunks = nesso::parser::Chunker::fromJsonFile(path, &stats, "message", 4);
    std::filesystem::remove_all(directory);
    REQUIRE(chunks.has_value());
    REQUIRE(chunks->size() == 1);
    REQUIRE((*chunks)[0].text == "abc\xC3\xA9");
    REQUIRE(stats.truncatedLines == 1);
}

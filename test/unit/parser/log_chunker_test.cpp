#include <catch2/catch_all.hpp>
#include <parser/log_chunker.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <streambuf>
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
}

class FailingInput final : public std::streambuf
{
  private:
    int_type underflow() override { throw std::ios_base::failure("read failed"); }
};

class FailingAfterPrefix final : public std::streambuf
{
  public:
    FailingAfterPrefix()
    {
        char *begin = prefix_.data();
        setg(begin, begin, begin + prefix_.size());
    }

  private:
    int_type underflow() override { throw std::ios_base::failure("read failed after probe"); }
    std::string prefix_ = std::string(4096, 'a');
};

class ScopedCin
{
  public:
    explicit ScopedCin(std::streambuf &buffer) : original_(std::cin.rdbuf()), state_(std::cin.rdstate())
    {
        std::cin.rdbuf(&buffer);
        std::cin.clear();
    }

    ~ScopedCin()
    {
        std::cin.rdbuf(original_);
        std::cin.clear(state_);
    }
    ScopedCin(const ScopedCin &) = delete;
    ScopedCin &operator=(const ScopedCin &) = delete;
    ScopedCin(ScopedCin &&) = delete;
    ScopedCin &operator=(ScopedCin &&) = delete;

  private:
    std::streambuf *original_;
    std::ios::iostate state_;
};

} // namespace

#ifndef NESSO_TEST_FIXTURES
#error "NESSO_TEST_FIXTURES must be defined"
#endif

TEST_CASE("ParsedChunk preserves positional aggregate initialization", "[Chunker][parser][Unit]")
{
    const nesso::parser::ParsedChunk chunk{"message", 7, 20, true};
    CHECK(chunk.text == "message");
    CHECK(chunk.lineNumber == 7);
    CHECK(chunk.byteOffset == 20);
    CHECK(chunk.arrayElement);
    CHECK_FALSE(chunk.skipped);
    CHECK(chunk.byteLength == 0);
}

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

TEST_CASE("Chunker reports stdin read failures", "[Chunker][parser][Unit]")
{
    SECTION("before the binary probe finishes")
    {
        FailingInput input;
        const ScopedCin redirected{input};
        const auto result =
            nesso::parser::Chunker::forEachRecord("-", 4096, nullptr, "message", [](nesso::parser::ParsedChunk) {});
        REQUIRE_FALSE(result);
        REQUIRE(result.error() == nesso::parser::ParseError::FileOpenFailure);
    }
    SECTION("after the binary probe finishes")
    {
        FailingAfterPrefix input;
        const ScopedCin redirected{input};
        const auto result = nesso::parser::Chunker::fromFile("-");
        REQUIRE_FALSE(result);
        REQUIRE(result.error() == nesso::parser::ParseError::FileOpenFailure);
    }
}

TEST_CASE("Chunker preserves stdin probe and record behavior", "[Chunker][parser][Unit]")
{
    SECTION("empty input")
    {
        std::istringstream input;
        const ScopedCin redirected{*input.rdbuf()};
        const auto chunks = nesso::parser::Chunker::fromFile("-");
        REQUIRE(chunks);
        REQUIRE(chunks->empty());
    }
    SECTION("an exact probe-sized input")
    {
        std::istringstream input{std::string(4096, 'a')};
        const ScopedCin redirected{*input.rdbuf()};
        const auto chunks = nesso::parser::Chunker::fromFile("-", 4096);
        REQUIRE(chunks);
        REQUIRE(chunks->size() == 1);
        REQUIRE(chunks->front().text == std::string(4096, 'a'));
    }
    SECTION("lines span the probe boundary")
    {
        std::istringstream input{std::string(4096, 'a') + "\nlast\n"};
        const ScopedCin redirected{*input.rdbuf()};
        std::vector<nesso::parser::ParsedChunk> records;
        REQUIRE(nesso::parser::Chunker::forEachRecord("-", 5, nullptr, "message",
                                                      [&records](nesso::parser::ParsedChunk record)
                                                      { records.push_back(std::move(record)); }));
        REQUIRE(records.size() == 2);
        REQUIRE(records[0].text == "aaaaa");
        REQUIRE(records[0].byteLength == 4096);
        REQUIRE(records[1].text == "last");
        REQUIRE(records[1].byteOffset == 4097);
    }
    SECTION("a NUL in the probe is rejected")
    {
        std::istringstream input{std::string("ab\0cd", 5)};
        const ScopedCin redirected{*input.rdbuf()};
        const auto chunks = nesso::parser::Chunker::fromFile("-");
        REQUIRE_FALSE(chunks);
        REQUIRE(chunks.error() == nesso::parser::ParseError::BinaryFile);
    }
    SECTION("a NUL after the probe retains the existing behavior")
    {
        std::string bytes(4096, 'a');
        bytes.push_back('\0');
        bytes += "tail";
        std::istringstream input{bytes};
        const ScopedCin redirected{*input.rdbuf()};
        const auto chunks = nesso::parser::Chunker::fromFile("-", 5000);
        REQUIRE(chunks);
        REQUIRE(chunks->size() == 1);
        REQUIRE(chunks->front().text == bytes);
    }
}

TEST_CASE("Chunker re-reads long records without splitting code points", "[Chunker][parser][Unit]")
{
    const auto path =
        std::filesystem::temp_directory_path() / ("nesso-long-record-" + std::to_string(getpid()) + ".log");
    const std::string line = "\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80" + std::string(200, 'z');
    writeBytes(path, line + "\r\n");
    const auto chunks = nesso::parser::Chunker::fromFile(path, 2);
    REQUIRE(chunks);
    REQUIRE(chunks->size() == 1);
    const auto &chunk = chunks->front();
    const auto truncated = nesso::parser::Chunker::readRecord(path, chunk.byteOffset, chunk.byteLength,
                                                              chunk.lineNumber, false, "message", 2);
    REQUIRE(truncated);
    REQUIRE(*truncated == chunk.text);
    REQUIRE(*truncated == "\xC3\xA9\xE2\x82\xAC");
    const auto complete = nesso::parser::Chunker::readRecord(path, chunk.byteOffset, chunk.byteLength, chunk.lineNumber,
                                                             false, "message", std::numeric_limits<size_t>::max());
    REQUIRE(complete);
    REQUIRE(*complete == line);
    const auto outOfRange =
        nesso::parser::Chunker::readRecord(path, std::numeric_limits<uint64_t>::max(), 1, 1, false, "message", 4096);
    REQUIRE_FALSE(outOfRange);
    REQUIRE(outOfRange.error() == nesso::parser::ParseError::FileOpenFailure);
    std::filesystem::remove(path);
}

TEST_CASE("Chunker rejects invalid JSON source ranges", "[Chunker][parser][Unit]")
{
    const auto path =
        std::filesystem::temp_directory_path() / ("nesso-invalid-json-range-" + std::to_string(getpid()) + ".json");
    writeBytes(path, "[{\"message\":\"first\"}]");
    const auto first = nesso::parser::Chunker::readRecord(path, 0, 0, 1, true, "message", 4096);
    REQUIRE(first);
    REQUIRE(*first == "first");
    const auto offset = nesso::parser::Chunker::readRecord(path, 1, 0, 1, true, "message", 4096);
    REQUIRE_FALSE(offset);
    REQUIRE(offset.error() == nesso::parser::ParseError::UnsupportedFormat);
    const auto length = nesso::parser::Chunker::readRecord(path, 0, 1, 1, true, "message", 4096);
    REQUIRE_FALSE(length);
    REQUIRE(length.error() == nesso::parser::ParseError::UnsupportedFormat);
    REQUIRE_FALSE(nesso::parser::Chunker::readRecord(path, 0, 0, 0, true, "message", 4096));
    writeBytes(path, "{\"message\":\"object\"}");
    REQUIRE_FALSE(nesso::parser::Chunker::readRecord(path, 0, 1, 1, false, "message", 4096));
    writeBytes(path, "{\"message\":42}");
    const auto nonString = nesso::parser::Chunker::readRecord(path, 0, 0, 1, false, "message", 4096);
    REQUIRE_FALSE(nonString);
    REQUIRE(nonString.error() == nesso::parser::ParseError::UnsupportedFormat);
    std::filesystem::remove(path);

    const auto jsonl =
        std::filesystem::temp_directory_path() / ("nesso-invalid-json-line-" + std::to_string(getpid()) + ".jsonl");
    constexpr std::string_view line = "{\"message\":42}";
    writeBytes(jsonl, line);
    const auto nonStringLine = nesso::parser::Chunker::readRecord(jsonl, 0, line.size(), 1, false, "message", 4096);
    REQUIRE_FALSE(nonStringLine);
    REQUIRE(nonStringLine.error() == nesso::parser::ParseError::UnsupportedFormat);
    std::filesystem::remove(jsonl);
}

TEST_CASE("Chunker reports unreadable JSON source records", "[Chunker][parser][Unit]")
{
    const auto directory =
        std::filesystem::temp_directory_path() / ("nesso-unreadable-json-" + std::to_string(getpid()) + ".json");
    REQUIRE(std::filesystem::create_directory(directory));
    const auto record = nesso::parser::Chunker::readRecord(directory, 0, 0, 1, false, "message", 4096);
    std::filesystem::remove(directory);
    REQUIRE_FALSE(record);
    REQUIRE(record.error() == nesso::parser::ParseError::FileOpenFailure);
}

TEST_CASE("Chunker does not re-read stdin records as files", "[Chunker][parser][Unit]")
{
    const auto record = nesso::parser::Chunker::readRecord("-", 0, 4, 1, false, "message", 4096);
    REQUIRE_FALSE(record);
    CHECK(record.error() == nesso::parser::ParseError::UnsupportedFormat);
}

TEST_CASE("Chunker reports non-rewindable file input", "[Chunker][parser][Unit]")
{
    int descriptors[2]{};
    REQUIRE(pipe(descriptors) == 0);
    constexpr std::string_view message = "message\n";
    REQUIRE(write(descriptors[1], message.data(), message.size()) == static_cast<ssize_t>(message.size()));
    REQUIRE(close(descriptors[1]) == 0);
    const auto path = std::filesystem::path("/proc/self/fd") / std::to_string(descriptors[0]);
    const auto chunks = nesso::parser::Chunker::fromFile(path);
    REQUIRE(close(descriptors[0]) == 0);
    REQUIRE_FALSE(chunks);
    REQUIRE(chunks.error() == nesso::parser::ParseError::FileOpenFailure);

    REQUIRE(pipe(descriptors) == 0);
    const auto source = std::filesystem::path("/proc/self/fd") / std::to_string(descriptors[0]);
    const auto record = nesso::parser::Chunker::readRecord(source, 0, 1, 1, false, "message", 4096);
    REQUIRE(close(descriptors[0]) == 0);
    REQUIRE(close(descriptors[1]) == 0);
    REQUIRE_FALSE(record);
    REQUIRE(record.error() == nesso::parser::ParseError::FileOpenFailure);
}

TEST_CASE("Chunker propagates consumer failures for kept and skipped records", "[Chunker][parser][Unit]")
{
    const auto directory =
        std::filesystem::temp_directory_path() / ("nesso-callback-errors-" + std::to_string(getpid()));
    REQUIRE(std::filesystem::create_directory(directory));
    const auto log = directory / "records.log";
    const auto jsonl = directory / "records.jsonl";
    const auto json = directory / "records.json";
    writeBytes(log, "kept\n\n");
    writeBytes(jsonl, "{bad\n");
    writeBytes(json, "[{\"message\":\"kept\"},{\"missing\":1}]");
    const auto failKept = [](nesso::parser::ParsedChunk) { throw std::runtime_error("consumer failed"); };
    const auto failSkipped = [](const nesso::parser::ParsedChunk &record)
    {
        if (record.skipped)
        {
            throw std::runtime_error("consumer failed");
        }
    };

    SECTION("kept log record")
    {
        REQUIRE_THROWS_AS(nesso::parser::Chunker::forEachRecord(log, 4096, nullptr, "message", failKept),
                          std::runtime_error);
    }
    SECTION("legacy kept-record API")
    {
        REQUIRE_THROWS_AS(nesso::parser::Chunker::forEach(log, 4096, nullptr, "message", failKept), std::runtime_error);
    }
    SECTION("skipped log record")
    {
        REQUIRE_THROWS_AS(nesso::parser::Chunker::forEachRecord(log, 4096, nullptr, "message", failSkipped),
                          std::runtime_error);
    }
    SECTION("kept stdin record")
    {
        std::istringstream input{"kept\n"};
        const ScopedCin redirected{*input.rdbuf()};
        REQUIRE_THROWS_AS(nesso::parser::Chunker::forEachRecord("-", 4096, nullptr, "message", failKept),
                          std::runtime_error);
    }
    SECTION("malformed JSONL record")
    {
        nesso::parser::ParseStats stats{};
        REQUIRE_THROWS_AS(nesso::parser::Chunker::forEachRecord(jsonl, 4096, &stats, "message", failSkipped),
                          std::runtime_error);
        REQUIRE(stats.malformedLines == 1);
    }
    SECTION("JSON array element with a missing field")
    {
        nesso::parser::ParseStats stats{};
        REQUIRE_THROWS_AS(nesso::parser::Chunker::forEachRecord(json, 4096, &stats, "message", failSkipped),
                          std::runtime_error);
        REQUIRE(stats.missingFieldLines == 1);
    }
    SECTION("kept JSON object")
    {
        writeBytes(json, "{\"message\":\"kept\"}");
        REQUIRE_THROWS_AS(nesso::parser::Chunker::forEachRecord(json, 4096, nullptr, "message", failKept),
                          std::runtime_error);
    }
    SECTION("consumer I/O exceptions are not input failures")
    {
        const auto failIo = [](nesso::parser::ParsedChunk) { throw std::ios_base::failure("consumer I/O failure"); };
        REQUIRE_THROWS_AS(nesso::parser::Chunker::forEachRecord(json, 4096, nullptr, "message", failIo),
                          std::ios_base::failure);
        REQUIRE_THROWS_AS(nesso::parser::Chunker::forEachRecord(jsonl, 4096, nullptr, "message", failIo),
                          std::ios_base::failure);
    }
    std::filesystem::remove_all(directory);
}

TEST_CASE("Chunker preserves kept records with a zero code-point limit", "[Chunker][parser][Unit]")
{
    const auto path =
        std::filesystem::temp_directory_path() / ("nesso-zero-limit-" + std::to_string(getpid()) + ".log");
    writeBytes(path, "value\n");
    nesso::parser::ParseStats stats{};
    const auto chunks = nesso::parser::Chunker::fromLogFile(path, 0, &stats);
    std::filesystem::remove(path);
    REQUIRE(chunks);
    REQUIRE(chunks->size() == 1);
    REQUIRE(chunks->front().text.empty());
    REQUIRE(stats.truncatedLines == 1);
}

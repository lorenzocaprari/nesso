#include <catch2/catch_all.hpp>
#include <parser/log_chunker.hpp>

#include <fcntl.h>
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

void WriteBytes(const std::filesystem::path &path, std::string_view bytes)
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
    CHECK(chunk.line_number == 7);
    CHECK(chunk.byte_offset == 20);
    CHECK(chunk.array_element);
    CHECK_FALSE(chunk.skipped);
    CHECK(chunk.byte_length == 0);
    CHECK(chunk.source.empty());
    CHECK_FALSE(chunk.entry_start);
}

TEST_CASE("Chunker parses .log files line-by-line", "[Chunker][parser][Unit]")
{
    const std::filesystem::path path = std::filesystem::path(NESSO_TEST_FIXTURES) / "sample.log";
    nesso::parser::ParseStats stats{};
    const auto chunks = nesso::parser::Chunker::FromLogFile(path, 64, &stats);
    REQUIRE(chunks.has_value());
    REQUIRE(chunks->size() == 3);
    REQUIRE((*chunks)[0].text == "alpha line");
    REQUIRE((*chunks)[0].line_number == 1);
    REQUIRE((*chunks)[0].byte_offset == 0);
    REQUIRE((*chunks)[1].text == "beta line that is intentionally very long and should be skipped ");
    REQUIRE((*chunks)[1].text.size() == 64);
    REQUIRE((*chunks)[2].text == "gamma line");
    REQUIRE((*chunks)[2].byte_offset == 209);
    REQUIRE(stats.skipped_lines == 2);
    REQUIRE(stats.empty_lines == 2);
    REQUIRE(stats.truncated_lines == 1);
}

TEST_CASE("Chunker skips ASCII-whitespace-only log lines", "[Chunker][parser][Unit]")
{
    const auto path =
        std::filesystem::temp_directory_path() / ("nesso-chunker-blank-" + std::to_string(getpid()) + ".log");
    const std::string source = std::string("\n \t\r\n\v\f\t \n  keep \t\n") + std::string(4097, ' ') + "\n\xC2\xA0\n\t";
    WriteBytes(path, source);
    nesso::parser::ParseStats stats{};
    const auto chunks = nesso::parser::Chunker::FromLogFile(path, 4096, &stats);
    std::filesystem::remove(path);

    REQUIRE(chunks.has_value());
    REQUIRE(chunks->size() == 2);
    CHECK((*chunks)[0].text == "  keep \t");
    CHECK((*chunks)[0].line_number == 4);
    CHECK((*chunks)[1].text == "\xC2\xA0");
    CHECK((*chunks)[1].line_number == 6);
    CHECK(stats.skipped_lines == 5);
    CHECK(stats.empty_lines == 5);
    CHECK(stats.truncated_lines == 0);
}

TEST_CASE("Chunker extracts message fields from jsonl", "[Chunker][parser][Unit]")
{
    const std::filesystem::path path = std::filesystem::path(NESSO_TEST_FIXTURES) / "sample.jsonl";
    nesso::parser::ParseStats stats{};
    const auto chunks = nesso::parser::Chunker::FromJsonFile(path, &stats);
    REQUIRE(chunks.has_value());
    REQUIRE(chunks->size() == 2);
    REQUIRE((*chunks)[0].text == "payment timeout after 30s");
    REQUIRE((*chunks)[0].byte_offset == 0);
    REQUIRE((*chunks)[1].text == "database connection refused");
    REQUIRE((*chunks)[1].byte_offset == 66);
    REQUIRE(stats.skipped_lines == 1);
    REQUIRE(stats.missing_field_lines == 1);
}

TEST_CASE("Chunker routes FromFile by extension", "[Chunker][parser][Unit]")
{
    const auto log_chunks = nesso::parser::Chunker::FromFile(std::filesystem::path(NESSO_TEST_FIXTURES) / "sample.log");
    REQUIRE(log_chunks.has_value());
    REQUIRE_FALSE(log_chunks->empty());

    const auto jsonl_chunks =
        nesso::parser::Chunker::FromFile(std::filesystem::path(NESSO_TEST_FIXTURES) / "sample.jsonl");
    REQUIRE(jsonl_chunks.has_value());
    REQUIRE(jsonl_chunks->size() == 2);

    nesso::parser::ParseStats stats{};
    const auto limited =
        nesso::parser::Chunker::FromFile(std::filesystem::path(NESSO_TEST_FIXTURES) / "sample.log", 64, &stats);
    REQUIRE(limited.has_value());
    REQUIRE(limited->size() == 3);
    REQUIRE(stats.skipped_lines == 2);
    REQUIRE(stats.truncated_lines == 1);
}

TEST_CASE("Chunker parses .json documents", "[Chunker][parser][Unit]")
{
    const auto single = nesso::parser::Chunker::FromFile(std::filesystem::path(NESSO_TEST_FIXTURES) / "sample.json");
    REQUIRE(single.has_value());
    REQUIRE(single->size() == 1);
    REQUIRE((*single)[0].text == "standalone entry");

    const auto array =
        nesso::parser::Chunker::FromFile(std::filesystem::path(NESSO_TEST_FIXTURES) / "sample_array.json");
    REQUIRE(array.has_value());
    REQUIRE(array->size() == 2);
    REQUIRE((*array)[0].text == "first entry");
    REQUIRE((*array)[1].text == "second entry");
}

TEST_CASE("Chunker skips malformed jsonl lines", "[Chunker][parser][Unit]")
{
    nesso::parser::ParseStats stats{};
    const auto chunks = nesso::parser::Chunker::FromJsonFile(
        std::filesystem::path(NESSO_TEST_FIXTURES) / "sample_malformed.jsonl", &stats);
    REQUIRE(chunks.has_value());
    REQUIRE(chunks->size() == 2);
    REQUIRE((*chunks)[0].text == "valid line");
    REQUIRE((*chunks)[1].text == "after bad line");
    REQUIRE(stats.skipped_lines == 1);
    REQUIRE(stats.malformed_lines == 1);
}

TEST_CASE("Chunker tolerates invalid json documents", "[Chunker][parser][Unit]")
{
    nesso::parser::ParseStats stats{};
    const auto chunks = nesso::parser::Chunker::FromJsonFile(
        std::filesystem::path(NESSO_TEST_FIXTURES) / "sample_invalid.json", &stats);
    REQUIRE(chunks.has_value());
    REQUIRE(chunks->empty());
    REQUIRE(stats.skipped_lines == 1);
    REQUIRE(stats.malformed_lines == 1);
}

TEST_CASE("Chunker reports file open failures", "[Chunker][parser][Unit]")
{
    const auto log_chunks = nesso::parser::Chunker::FromLogFile("missing-sample.log");
    REQUIRE_FALSE(log_chunks.has_value());
    REQUIRE(log_chunks.error() == nesso::parser::ParseError::FileOpenFailure);

    const auto json_chunks = nesso::parser::Chunker::FromJsonFile("missing-sample.json");
    REQUIRE_FALSE(json_chunks.has_value());
    REQUIRE(json_chunks.error() == nesso::parser::ParseError::FileOpenFailure);
}

TEST_CASE("Chunker reads any non-JSON name as lines", "[Chunker][parser][Unit]")
{
    const auto directory = std::filesystem::temp_directory_path() / "nesso-chunker-names";
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);

    const auto expect_line = [&](const char *name)
    {
        const auto path = directory / name;
        WriteBytes(path, "database connection refused\n");
        const auto chunks = nesso::parser::Chunker::FromFile(path);
        REQUIRE(chunks.has_value());
        REQUIRE(chunks->size() == 1);
        REQUIRE((*chunks)[0].text == "database connection refused");
        REQUIRE_FALSE((*chunks)[0].array_element);
    };
    expect_line("syslog");
    expect_line("APP.LOG");
    expect_line("app.log.1");
    expect_line("notes.txt");

    const auto json_path = directory / "rows.JSON";
    WriteBytes(json_path, "{\"message\":\"standalone entry\"}\n");
    const auto json_chunks = nesso::parser::Chunker::FromFile(json_path);
    REQUIRE(json_chunks.has_value());
    REQUIRE(json_chunks->size() == 1);
    REQUIRE((*json_chunks)[0].text == "standalone entry");

    const auto jsonl_path = directory / "rows.JSONL";
    WriteBytes(jsonl_path, "{\"message\":\"one\"}\n{\"message\":\"two\"}\n");
    const auto jsonl_chunks = nesso::parser::Chunker::FromFile(jsonl_path);
    REQUIRE(jsonl_chunks.has_value());
    REQUIRE(jsonl_chunks->size() == 2);

    std::filesystem::remove_all(directory);
}

TEST_CASE("Chunker rejects a NUL in the first 4 KiB", "[Chunker][parser][Unit]")
{
    const auto directory = std::filesystem::temp_directory_path() / "nesso-chunker-binary";
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);

    const auto binary = directory / "blob.log";
    WriteBytes(binary, std::string("hello\0world", 11));
    const auto rejected = nesso::parser::Chunker::FromFile(binary);
    REQUIRE_FALSE(rejected.has_value());
    REQUIRE(rejected.error() == nesso::parser::ParseError::BinaryFile);

    std::string late(4096, 'a');
    late.push_back('\0');
    late += "tail\n";
    const auto after = directory / "late.log";
    WriteBytes(after, late);
    const auto kept = nesso::parser::Chunker::FromFile(after);
    REQUIRE(kept.has_value());
    REQUIRE_FALSE(kept->empty());

    std::filesystem::remove_all(directory);
}

TEST_CASE("Chunker numbers JSON array elements", "[Chunker][parser][Unit]")
{
    const auto array =
        nesso::parser::Chunker::FromFile(std::filesystem::path(NESSO_TEST_FIXTURES) / "sample_array.json");
    REQUIRE(array.has_value());
    REQUIRE(array->size() == 2);
    REQUIRE((*array)[0].array_element);
    REQUIRE((*array)[0].line_number == 1);
    REQUIRE((*array)[1].array_element);
    REQUIRE((*array)[1].line_number == 3);
}

TEST_CASE("Chunker keeps a final line that has no trailing newline", "[Chunker][parser][Unit]")
{
    const auto directory = std::filesystem::temp_directory_path() / "nesso-chunker-eof";
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);

    const auto log_path = directory / "tail.log";
    WriteBytes(log_path, "alpha\nbeta");
    const auto log_chunks = nesso::parser::Chunker::FromLogFile(log_path);
    REQUIRE(log_chunks.has_value());
    REQUIRE(log_chunks->size() == 2);
    REQUIRE((*log_chunks)[1].text == "beta");
    REQUIRE((*log_chunks)[1].byte_offset == 6);

    const auto jsonl_path = directory / "tail.jsonl";
    WriteBytes(jsonl_path, "{\"message\":\"only\"}");
    const auto jsonl_chunks = nesso::parser::Chunker::FromJsonFile(jsonl_path);
    REQUIRE(jsonl_chunks.has_value());
    REQUIRE(jsonl_chunks->size() == 1);
    REQUIRE((*jsonl_chunks)[0].text == "only");

    std::filesystem::remove_all(directory);
}

TEST_CASE("Chunker skips blank jsonl lines and non-string messages", "[Chunker][parser][Unit]")
{
    const auto directory = std::filesystem::temp_directory_path() / "nesso-chunker-skip";
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);

    const auto blank = directory / "blank.jsonl";
    WriteBytes(blank, "{\"message\":\"one\"}\n\n{\"message\":\"two\"}\n");

    nesso::parser::ParseStats stats{};
    const auto counted = nesso::parser::Chunker::FromJsonFile(blank, &stats);
    REQUIRE(counted.has_value());
    REQUIRE(counted->size() == 2);
    REQUIRE(stats.skipped_lines == 1);

    const auto uncounted = nesso::parser::Chunker::FromFile(blank);
    REQUIRE(uncounted.has_value());
    REQUIRE(uncounted->size() == 2);

    const auto numeric = directory / "numeric.jsonl";
    WriteBytes(numeric, "{\"message\":1}\n{\"message\":\"kept\"}\n");
    nesso::parser::ParseStats numeric_stats{};
    const auto numeric_chunks = nesso::parser::Chunker::FromJsonFile(numeric, &numeric_stats);
    REQUIRE(numeric_chunks.has_value());
    REQUIRE(numeric_chunks->size() == 1);
    REQUIRE((*numeric_chunks)[0].text == "kept");
    REQUIRE(numeric_stats.skipped_lines == 1);

    std::filesystem::remove_all(directory);
}

TEST_CASE("Chunker skips blank JSON lines and string values", "[Chunker][parser][Unit]")
{
    const auto jsonl =
        std::filesystem::temp_directory_path() / ("nesso-chunker-blank-" + std::to_string(getpid()) + ".jsonl");
    WriteBytes(jsonl, " \t\r\n"
                      "{\"msg\":\"\"}\n"
                      "{\"msg\":\" \\t\\r\\n\\f\\u000b\"}\n"
                      "{\"msg\":\"  keep \\t\"}\n"
                      "{\"msg\":\"\\u00a0\"}\n"
                      "{\"message\":\"missing\"}\n"
                      "{bad\n"
                      "\t");
    nesso::parser::ParseStats jsonl_stats{};
    const auto lines = nesso::parser::Chunker::FromJsonFile(jsonl, &jsonl_stats, "msg");
    const auto uncounted = nesso::parser::Chunker::FromJsonFile(jsonl, nullptr, "msg");
    std::filesystem::remove(jsonl);

    REQUIRE(lines.has_value());
    REQUIRE(lines->size() == 2);
    REQUIRE(uncounted.has_value());
    CHECK(uncounted->size() == 2);
    CHECK((*lines)[0].text == "  keep \t");
    CHECK((*lines)[0].line_number == 4);
    CHECK((*lines)[1].text == "\xC2\xA0");
    CHECK((*lines)[1].line_number == 5);
    CHECK(jsonl_stats.skipped_lines == 6);
    CHECK(jsonl_stats.empty_lines == 4);
    CHECK(jsonl_stats.malformed_lines == 1);
    CHECK(jsonl_stats.missing_field_lines == 1);

    const auto json =
        std::filesystem::temp_directory_path() / ("nesso-chunker-blank-" + std::to_string(getpid()) + ".json");
    WriteBytes(json, "{\"msg\":\"" + std::string(4097, ' ') + "\"}");
    nesso::parser::ParseStats object_stats{};
    const auto object = nesso::parser::Chunker::FromJsonFile(json, &object_stats, "msg");
    REQUIRE(object.has_value());
    CHECK(object->empty());
    CHECK(object_stats.skipped_lines == 1);
    CHECK(object_stats.empty_lines == 1);
    CHECK(object_stats.truncated_lines == 0);

    WriteBytes(json, "[{\"msg\":\"\"},{\"msg\":\" \\t\"},{\"msg\":\"keep\"}]");
    nesso::parser::ParseStats array_stats{};
    const auto array = nesso::parser::Chunker::FromJsonFile(json, &array_stats, "msg");
    std::filesystem::remove(json);
    REQUIRE(array.has_value());
    REQUIRE(array->size() == 1);
    CHECK(array->front().text == "keep");
    CHECK(array->front().line_number == 3);
    CHECK(array->front().array_element);
    CHECK(array_stats.skipped_lines == 2);
    CHECK(array_stats.empty_lines == 2);
}

TEST_CASE("Chunker skips invalid documents when stats are not collected", "[Chunker][parser][Unit]")
{
    const auto directory = std::filesystem::temp_directory_path() / "nesso-chunker-invalid";
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);

    const auto jsonl = directory / "bad.jsonl";
    WriteBytes(jsonl, "{not valid\n{\"message\":\"kept\"}\n");
    const auto jsonl_chunks = nesso::parser::Chunker::FromFile(jsonl);
    REQUIRE(jsonl_chunks.has_value());
    REQUIRE(jsonl_chunks->size() == 1);
    REQUIRE((*jsonl_chunks)[0].text == "kept");

    const auto json = directory / "bad.json";
    WriteBytes(json, "{not valid");
    const auto json_chunks = nesso::parser::Chunker::FromFile(json);
    REQUIRE(json_chunks.has_value());
    REQUIRE(json_chunks->empty());

    std::filesystem::remove_all(directory);
}

TEST_CASE("Chunker strips a carriage return and reads the selected JSON field", "[Chunker][parser][Unit]")
{
    const auto directory = std::filesystem::temp_directory_path() / "nesso-chunker-field";
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);

    const auto log_path = directory / "crlf.log";
    WriteBytes(log_path, "hello\r\nworld\r\n");
    const auto log_chunks = nesso::parser::Chunker::FromLogFile(log_path);
    REQUIRE(log_chunks.has_value());
    REQUIRE(log_chunks->size() == 2);
    REQUIRE((*log_chunks)[0].text == "hello");
    REQUIRE((*log_chunks)[1].text == "world");
    REQUIRE((*log_chunks)[1].byte_offset == 7);

    const auto jsonl_path = directory / "fields.jsonl";
    WriteBytes(jsonl_path, "{\"message\":\"ignored\",\"msg\":\"kept\"}\r\n{\"msg\":1}\n");
    nesso::parser::ParseStats stats{};
    const auto jsonl_chunks = nesso::parser::Chunker::FromJsonFile(jsonl_path, &stats, "msg");
    REQUIRE(jsonl_chunks.has_value());
    REQUIRE(jsonl_chunks->size() == 1);
    REQUIRE((*jsonl_chunks)[0].text == "kept");
    REQUIRE(stats.skipped_lines == 1);

    const auto long_value = directory / "long.json";
    WriteBytes(long_value, "{\"message\":\"" + std::string(80, 'a') + "\"}");
    nesso::parser::ParseStats long_stats{};
    const auto long_chunks = nesso::parser::Chunker::FromJsonFile(long_value, &long_stats, "message", 8);
    REQUIRE(long_chunks.has_value());
    REQUIRE(long_chunks->size() == 1);
    REQUIRE((*long_chunks)[0].text == "aaaaaaaa");
    REQUIRE(long_stats.truncated_lines == 1);
    REQUIRE(long_stats.skipped_lines == 0);

    std::filesystem::remove_all(directory);
}

namespace
{

struct Limited
{
    std::string text;
    size_t truncated = 0;
};

Limited LimitedLine(const std::string &line, size_t max_line_length)
{
    const auto directory = std::filesystem::temp_directory_path() / "nesso-chunker-limit";
    std::filesystem::create_directories(directory);
    const auto path = directory / "line.log";
    WriteBytes(path, line + "\n");
    nesso::parser::ParseStats stats{};
    const auto chunks = nesso::parser::Chunker::FromLogFile(path, max_line_length, &stats);
    std::filesystem::remove_all(directory);
    REQUIRE(chunks.has_value());
    REQUIRE(chunks->size() == 1);
    return Limited{.text = (*chunks)[0].text, .truncated = stats.truncated_lines};
}

} // namespace

TEST_CASE("Chunker truncates at code points, not bytes", "[Chunker][parser][Unit]")
{
    SECTION("ASCII keeps the byte behaviour around the limit")
    {
        REQUIRE(LimitedLine(std::string(4095, 'a'), 4096).truncated == 0);
        REQUIRE(LimitedLine(std::string(4096, 'a'), 4096).truncated == 0);
        const auto cut = LimitedLine(std::string(4097, 'a'), 4096);
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
        const auto kept = LimitedLine(text, 4096);
        REQUIRE(kept.text == text);
        REQUIRE(kept.truncated == 0);
    }

    SECTION("a 2-, 3- or 4-byte character on the boundary is kept whole")
    {
        for (const std::string character : {"\xC3\xA9", "\xE2\x82\xAC", "\xF0\x9F\x98\x80"})
        {
            const std::string prefix(3, 'a');
            const auto cut = LimitedLine(prefix + character + "zz", 4);
            REQUIRE(cut.text == prefix + character);
            REQUIRE(cut.truncated == 1);
        }
    }

    SECTION("a character after the limit is dropped whole")
    {
        const auto cut = LimitedLine("abcd\xF0\x9F\x98\x80", 4);
        REQUIRE(cut.text == "abcd");
        REQUIRE(cut.truncated == 1);
    }

    SECTION("invalid bytes count one each")
    {
        const auto lone = LimitedLine("\x80\x80\x80\x80\x80", 4);
        REQUIRE(lone.text == "\x80\x80\x80\x80");
        REQUIRE(lone.truncated == 1);

        const std::string partial = "a\xE2\x82";
        const auto broken = LimitedLine(partial + "bcd", 4);
        REQUIRE(broken.text == partial + "b");
        REQUIRE(broken.truncated == 1);
    }

    SECTION("a sequence cut short by the end of the line counts byte by byte")
    {
        const auto cut = LimitedLine("ab\xF0\x9F", 3);
        REQUIRE(cut.text == "ab\xF0");
        REQUIRE(cut.truncated == 1);
    }
}

TEST_CASE("Chunker truncates JSON values at code points", "[Chunker][parser][Unit]")
{
    const auto directory = std::filesystem::temp_directory_path() / "nesso-chunker-json-limit";
    std::filesystem::create_directories(directory);
    const auto path = directory / "values.jsonl";
    WriteBytes(path, "{\"message\":\"abc\\u00e9\\u00e9\"}\n");
    nesso::parser::ParseStats stats{};
    const auto chunks = nesso::parser::Chunker::FromJsonFile(path, &stats, "message", 4);
    std::filesystem::remove_all(directory);
    REQUIRE(chunks.has_value());
    REQUIRE(chunks->size() == 1);
    REQUIRE((*chunks)[0].text == "abc\xC3\xA9");
    REQUIRE(stats.truncated_lines == 1);
}

TEST_CASE("Chunker reports stdin read failures", "[Chunker][parser][Unit]")
{
    SECTION("before the binary probe finishes")
    {
        FailingInput input;
        const ScopedCin redirected{input};
        const auto result =
            nesso::parser::Chunker::ForEachRecord("-", 4096, nullptr, "message", [](nesso::parser::ParsedChunk) {});
        REQUIRE_FALSE(result);
        REQUIRE(result.error() == nesso::parser::ParseError::FileOpenFailure);
    }
    SECTION("after the binary probe finishes")
    {
        FailingAfterPrefix input;
        const ScopedCin redirected{input};
        const auto result = nesso::parser::Chunker::FromFile("-");
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
        const auto chunks = nesso::parser::Chunker::FromFile("-");
        REQUIRE(chunks);
        REQUIRE(chunks->empty());
    }
    SECTION("whitespace-only lines")
    {
        std::istringstream input{" \t\r\n  kept \t\n \t"};
        const ScopedCin redirected{*input.rdbuf()};
        nesso::parser::ParseStats stats{};
        const auto chunks = nesso::parser::Chunker::FromFile("-", 4096, &stats);
        REQUIRE(chunks);
        REQUIRE(chunks->size() == 1);
        CHECK(chunks->front().text == "  kept \t");
        CHECK(chunks->front().line_number == 2);
        CHECK(stats.skipped_lines == 2);
        CHECK(stats.empty_lines == 2);
    }
    SECTION("an exact probe-sized input")
    {
        std::istringstream input{std::string(4096, 'a')};
        const ScopedCin redirected{*input.rdbuf()};
        const auto chunks = nesso::parser::Chunker::FromFile("-", 4096);
        REQUIRE(chunks);
        REQUIRE(chunks->size() == 1);
        REQUIRE(chunks->front().text == std::string(4096, 'a'));
    }
    SECTION("lines span the probe boundary")
    {
        std::istringstream input{std::string(4096, 'a') + "\nlast\n"};
        const ScopedCin redirected{*input.rdbuf()};
        std::vector<nesso::parser::ParsedChunk> records;
        REQUIRE(nesso::parser::Chunker::ForEachRecord("-", 5, nullptr, "message",
                                                      [&records](nesso::parser::ParsedChunk record)
                                                      { records.push_back(std::move(record)); }));
        REQUIRE(records.size() == 2);
        REQUIRE(records[0].text == "aaaaa");
        REQUIRE(records[0].byte_length == 4096);
        REQUIRE(records[1].text == "last");
        REQUIRE(records[1].byte_offset == 4097);
    }
    SECTION("a NUL in the probe is rejected")
    {
        std::istringstream input{std::string("ab\0cd", 5)};
        const ScopedCin redirected{*input.rdbuf()};
        const auto chunks = nesso::parser::Chunker::FromFile("-");
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
        const auto chunks = nesso::parser::Chunker::FromFile("-", 5000);
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
    WriteBytes(path, line + "\r\n");
    const auto chunks = nesso::parser::Chunker::FromFile(path, 2);
    REQUIRE(chunks);
    REQUIRE(chunks->size() == 1);
    const auto &chunk = chunks->front();
    const auto truncated = nesso::parser::Chunker::ReadRecord(path,
                                                              {.text = {},
                                                               .line_number = chunk.line_number,
                                                               .byte_offset = chunk.byte_offset,
                                                               .array_element = false,
                                                               .byte_length = chunk.byte_length},
                                                              "message", 2);
    REQUIRE(truncated);
    REQUIRE(*truncated == chunk.text);
    REQUIRE(*truncated == "\xC3\xA9\xE2\x82\xAC");
    const auto complete = nesso::parser::Chunker::ReadRecord(path,
                                                             {.text = {},
                                                              .line_number = chunk.line_number,
                                                              .byte_offset = chunk.byte_offset,
                                                              .array_element = false,
                                                              .byte_length = chunk.byte_length},
                                                             "message", std::numeric_limits<size_t>::max());
    REQUIRE(complete);
    REQUIRE(*complete == line);
    const auto out_of_range = nesso::parser::Chunker::ReadRecord(path,
                                                                 {.text = {},
                                                                  .line_number = 1,
                                                                  .byte_offset = std::numeric_limits<uint64_t>::max(),
                                                                  .array_element = false,
                                                                  .byte_length = 1},
                                                                 "message", 4096);
    REQUIRE_FALSE(out_of_range);
    REQUIRE(out_of_range.error() == nesso::parser::ParseError::FileOpenFailure);
    std::filesystem::remove(path);
}

TEST_CASE("Chunker rejects invalid JSON source ranges", "[Chunker][parser][Unit]")
{
    const auto path =
        std::filesystem::temp_directory_path() / ("nesso-invalid-json-range-" + std::to_string(getpid()) + ".json");
    WriteBytes(path, "[{\"message\":\"first\"}]");
    const auto first = nesso::parser::Chunker::ReadRecord(
        path, {.text = {}, .line_number = 1, .byte_offset = 0, .array_element = true, .byte_length = 0}, "message",
        4096);
    REQUIRE(first);
    REQUIRE(*first == "first");
    const auto offset = nesso::parser::Chunker::ReadRecord(
        path, {.text = {}, .line_number = 1, .byte_offset = 1, .array_element = true, .byte_length = 0}, "message",
        4096);
    REQUIRE_FALSE(offset);
    REQUIRE(offset.error() == nesso::parser::ParseError::UnsupportedFormat);
    const auto length = nesso::parser::Chunker::ReadRecord(
        path, {.text = {}, .line_number = 1, .byte_offset = 0, .array_element = true, .byte_length = 1}, "message",
        4096);
    REQUIRE_FALSE(length);
    REQUIRE(length.error() == nesso::parser::ParseError::UnsupportedFormat);
    REQUIRE_FALSE(nesso::parser::Chunker::ReadRecord(
        path, {.text = {}, .line_number = 0, .byte_offset = 0, .array_element = true, .byte_length = 0}, "message",
        4096));
    WriteBytes(path, "{\"message\":\"object\"}");
    REQUIRE_FALSE(nesso::parser::Chunker::ReadRecord(
        path, {.text = {}, .line_number = 1, .byte_offset = 0, .array_element = false, .byte_length = 1}, "message",
        4096));
    WriteBytes(path, "{\"message\":42}");
    const auto non_string = nesso::parser::Chunker::ReadRecord(
        path, {.text = {}, .line_number = 1, .byte_offset = 0, .array_element = false, .byte_length = 0}, "message",
        4096);
    REQUIRE_FALSE(non_string);
    REQUIRE(non_string.error() == nesso::parser::ParseError::UnsupportedFormat);
    std::filesystem::remove(path);

    const auto jsonl =
        std::filesystem::temp_directory_path() / ("nesso-invalid-json-line-" + std::to_string(getpid()) + ".jsonl");
    constexpr std::string_view line = "{\"message\":42}";
    WriteBytes(jsonl, line);
    const auto non_string_line = nesso::parser::Chunker::ReadRecord(
        jsonl, {.text = {}, .line_number = 1, .byte_offset = 0, .array_element = false, .byte_length = line.size()},
        "message", 4096);
    REQUIRE_FALSE(non_string_line);
    REQUIRE(non_string_line.error() == nesso::parser::ParseError::UnsupportedFormat);
    std::filesystem::remove(jsonl);
}

TEST_CASE("Chunker reports unreadable JSON source records", "[Chunker][parser][Unit]")
{
    const auto directory =
        std::filesystem::temp_directory_path() / ("nesso-unreadable-json-" + std::to_string(getpid()) + ".json");
    REQUIRE(std::filesystem::create_directory(directory));
    const auto record = nesso::parser::Chunker::ReadRecord(
        directory, {.text = {}, .line_number = 1, .byte_offset = 0, .array_element = false, .byte_length = 0},
        "message", 4096);
    std::filesystem::remove(directory);
    REQUIRE_FALSE(record);
    REQUIRE(record.error() == nesso::parser::ParseError::FileOpenFailure);
}

TEST_CASE("Chunker does not re-read stdin records as files", "[Chunker][parser][Unit]")
{
    const auto record = nesso::parser::Chunker::ReadRecord(
        "-", {.text = {}, .line_number = 1, .byte_offset = 0, .array_element = false, .byte_length = 4}, "message",
        4096);
    REQUIRE_FALSE(record);
    CHECK(record.error() == nesso::parser::ParseError::UnsupportedFormat);
}

TEST_CASE("Chunker reports non-rewindable file input", "[Chunker][parser][Unit]")
{
    int descriptors[2]{};
    REQUIRE(pipe2(descriptors, O_CLOEXEC) == 0);
    constexpr std::string_view message = "message\n";
    REQUIRE(write(descriptors[1], message.data(), message.size()) == static_cast<ssize_t>(message.size()));
    REQUIRE(close(descriptors[1]) == 0);
    const auto path = std::filesystem::path("/proc/self/fd") / std::to_string(descriptors[0]);
    const auto chunks = nesso::parser::Chunker::FromFile(path);
    REQUIRE(close(descriptors[0]) == 0);
    REQUIRE_FALSE(chunks);
    REQUIRE(chunks.error() == nesso::parser::ParseError::FileOpenFailure);

    REQUIRE(pipe2(descriptors, O_CLOEXEC) == 0);
    const auto source = std::filesystem::path("/proc/self/fd") / std::to_string(descriptors[0]);
    const auto record = nesso::parser::Chunker::ReadRecord(
        source, {.text = {}, .line_number = 1, .byte_offset = 0, .array_element = false, .byte_length = 1}, "message",
        4096);
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
    WriteBytes(log, "kept\n\n");
    WriteBytes(jsonl, "{bad\n");
    WriteBytes(json, "[{\"message\":\"kept\"},{\"missing\":1}]");
    const auto fail_kept = [](nesso::parser::ParsedChunk) { throw std::runtime_error("consumer failed"); };
    const auto fail_skipped = [](const nesso::parser::ParsedChunk &record)
    {
        if (record.skipped)
        {
            throw std::runtime_error("consumer failed");
        }
    };

    SECTION("kept log record")
    {
        REQUIRE_THROWS_AS(nesso::parser::Chunker::ForEachRecord(log, 4096, nullptr, "message", fail_kept),
                          std::runtime_error);
    }
    SECTION("legacy kept-record API")
    {
        REQUIRE_THROWS_AS(nesso::parser::Chunker::ForEach(log, 4096, nullptr, "message", fail_kept),
                          std::runtime_error);
    }
    SECTION("skipped log record")
    {
        REQUIRE_THROWS_AS(nesso::parser::Chunker::ForEachRecord(log, 4096, nullptr, "message", fail_skipped),
                          std::runtime_error);
    }
    SECTION("kept stdin record")
    {
        std::istringstream input{"kept\n"};
        const ScopedCin redirected{*input.rdbuf()};
        REQUIRE_THROWS_AS(nesso::parser::Chunker::ForEachRecord("-", 4096, nullptr, "message", fail_kept),
                          std::runtime_error);
    }
    SECTION("malformed JSONL record")
    {
        nesso::parser::ParseStats stats{};
        REQUIRE_THROWS_AS(nesso::parser::Chunker::ForEachRecord(jsonl, 4096, &stats, "message", fail_skipped),
                          std::runtime_error);
        REQUIRE(stats.malformed_lines == 1);
    }
    SECTION("JSON array element with a missing field")
    {
        nesso::parser::ParseStats stats{};
        REQUIRE_THROWS_AS(nesso::parser::Chunker::ForEachRecord(json, 4096, &stats, "message", fail_skipped),
                          std::runtime_error);
        REQUIRE(stats.missing_field_lines == 1);
    }
    SECTION("kept JSON object")
    {
        WriteBytes(json, "{\"message\":\"kept\"}");
        REQUIRE_THROWS_AS(nesso::parser::Chunker::ForEachRecord(json, 4096, nullptr, "message", fail_kept),
                          std::runtime_error);
    }
    SECTION("consumer I/O exceptions are not input failures")
    {
        const auto fail_io = [](nesso::parser::ParsedChunk) { throw std::ios_base::failure("consumer I/O failure"); };
        REQUIRE_THROWS_AS(nesso::parser::Chunker::ForEachRecord(json, 4096, nullptr, "message", fail_io),
                          std::ios_base::failure);
        REQUIRE_THROWS_AS(nesso::parser::Chunker::ForEachRecord(jsonl, 4096, nullptr, "message", fail_io),
                          std::ios_base::failure);
    }
    std::filesystem::remove_all(directory);
}

TEST_CASE("Chunker preserves kept records with a zero code-point limit", "[Chunker][parser][Unit]")
{
    const auto path =
        std::filesystem::temp_directory_path() / ("nesso-zero-limit-" + std::to_string(getpid()) + ".log");
    WriteBytes(path, "value\n");
    nesso::parser::ParseStats stats{};
    const auto chunks = nesso::parser::Chunker::FromLogFile(path, 0, &stats);
    std::filesystem::remove(path);
    REQUIRE(chunks);
    REQUIRE(chunks->size() == 1);
    REQUIRE(chunks->front().text.empty());
    REQUIRE(stats.truncated_lines == 1);
}

TEST_CASE("Chunker skips a JSON document that is not an object or array", "[Chunker][parser][Unit]")
{
    const auto directory =
        std::filesystem::temp_directory_path() / ("nesso-chunker-json-scalar-" + std::to_string(getpid()));
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);

    for (const char *body : {"null", "42"})
    {
        const auto path = directory / (std::string(body) + ".json");
        WriteBytes(path, body);
        nesso::parser::ParseStats stats{};
        const auto chunks = nesso::parser::Chunker::FromFile(path, 4096, &stats);
        REQUIRE(chunks);
        CHECK(chunks->empty());
        CHECK(stats.skipped_lines == 1);
        CHECK(stats.missing_field_lines == 1);
    }

    std::filesystem::remove_all(directory);
}

TEST_CASE("Chunker skips non-object JSON array elements", "[Chunker][parser][Unit]")
{
    const auto path = std::filesystem::temp_directory_path() /
                      ("nesso-chunker-json-array-mixed-" + std::to_string(getpid()) + ".json");
    WriteBytes(path, "[1,{\"message\":\"ok\"}]");
    nesso::parser::ParseStats stats{};
    std::vector<nesso::parser::ParsedChunk> records;
    const auto walked = nesso::parser::Chunker::ForEachRecord(path, 4096, &stats, "message",
                                                              [&records](nesso::parser::ParsedChunk chunk)
                                                              { records.push_back(std::move(chunk)); });
    std::filesystem::remove(path);
    REQUIRE(walked);
    REQUIRE(records.size() == 2);
    CHECK(records[0].skipped);
    CHECK(records[0].array_element);
    CHECK(records[0].line_number == 1);
    CHECK_FALSE(records[1].skipped);
    CHECK(records[1].array_element);
    CHECK(records[1].line_number == 2);
    CHECK(records[1].text == "ok");
    CHECK(stats.skipped_lines == 1);
    CHECK(stats.missing_field_lines == 1);
}

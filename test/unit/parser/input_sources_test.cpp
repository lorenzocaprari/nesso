// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_all.hpp>
#include <parser/gzip_reader.hpp>
#include <parser/input_walk.hpp>
#include <parser/log_chunker.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace
{

class TempDir
{
  public:
    explicit TempDir(const std::string &name)
        : path_(std::filesystem::temp_directory_path() / (name + "-" + std::to_string(getpid())))
    {
        std::filesystem::remove_all(path_);
        std::filesystem::create_directories(path_);
    }

    TempDir(const TempDir &) = delete;
    TempDir &operator=(const TempDir &) = delete;
    ~TempDir() { std::filesystem::remove_all(path_); }

    [[nodiscard]] const std::filesystem::path &path() const { return path_; }

  private:
    std::filesystem::path path_;
};

void writeBytes(const std::filesystem::path &path, std::string_view bytes)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    REQUIRE(output);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    REQUIRE(output);
}

std::string unhex(std::string_view hex)
{
    std::string out;
    out.reserve(hex.size() / 2);
    const auto nibble = [](char character) -> int
    {
        if (character >= '0' && character <= '9')
        {
            return character - '0';
        }
        if (character >= 'a' && character <= 'f')
        {
            return character - 'a' + 10;
        }
        return -1;
    };
    for (std::size_t index = 0; index + 1 < hex.size(); index += 2)
    {
        const int high = nibble(hex[index]);
        const int low = nibble(hex[index + 1]);
        REQUIRE(high >= 0);
        REQUIRE(low >= 0);
        out.push_back(static_cast<char>((high << 4) | low));
    }
    return out;
}

const std::string_view GZIP_DATABASE =
    "1f8b0800f722f26a02ff4b492c494c4a2c4e5548cecfcb4b4d2ec9cccf53284a4d2b2d4e4de10200e756bf4f1c000000";
const std::string_view GZIP_CONCAT =
    "1f8b0800f722f26a02ffcb48cdc9c9070086a61036050000001f8b0800f722f26a02ff5328cf2fca49e10200e9262b3e07000000";
const std::string_view GZIP_BINARY = "1f8b0800f722f26a02ff4b4c4a6648494de30200d41300c208000000";
const std::string_view GZIP_JSON = "1f8b0800f722f26a02ffab56ca4d2d2e4e4c4f55b2524a492c494c4a2c4e5548cecfcb4b4d2ec9cccf5"
                                   "3284a4d2b2d4e4d51aae50200087aaacb2a000000";
const std::string_view GZIP_JSONL =
    "1f8b0800f722f26a02ffab56ca4d2d2e4e4c4f55b2524acb2c2a2e51aae5aa46122b4e4dcecf4b51c8c9cc4b05ca0000772595d92e000000";
const std::string_view GZIP_EMPTY = "1f8b0800f722f26a02ff03000000000000000000";
const std::string_view GZIP_TWO = "1f8b0800f722f26a02ffcbcf4be52a29cfe70200e60bba1208000000";

std::vector<std::string> textsOf(const std::filesystem::path &path)
{
    const auto chunks = nesso::parser::Chunker::fromFile(path);
    REQUIRE(chunks);
    std::vector<std::string> texts;
    for (const nesso::parser::ParsedChunk &chunk : *chunks)
    {
        texts.push_back(chunk.text);
    }
    return texts;
}

} // namespace

TEST_CASE("expandInputs keeps a file and stdin in argument order", "[parser][walk]")
{
    const TempDir dir{"nesso-walk-file"};
    const auto first = dir.path() / "z.log";
    const auto second = dir.path() / "a.log";
    writeBytes(first, "z\n");
    writeBytes(second, "a\n");
    const std::filesystem::path stdinPath{"-"};
    const auto expanded = nesso::parser::expandInputs(std::vector<std::filesystem::path>{first, stdinPath, second});
    REQUIRE(expanded);
    REQUIRE(expanded->size() == 3);
    CHECK(expanded->at(0) == first);
    CHECK(expanded->at(1) == "-");
    CHECK(expanded->at(2) == second);
    CHECK(expanded->front().string() == first.string());
}

TEST_CASE("expandInputs sorts a directory by unsigned path bytes", "[parser][walk]")
{
    const TempDir dir{"nesso-walk-sort"};
    const auto root = dir.path() / "logs";
    writeBytes(root / "b.log", "b\n");
    writeBytes(root / "nested" / "c.log", "c\n");
    writeBytes(root / "a.log", "a\n");
    writeBytes(root / ".hidden.log", "h\n");
    const std::string highName(1, static_cast<char>(0xFF));
    writeBytes(root / (highName + ".log"), "high\n");
    const auto pipe = root / "pipe";
    REQUIRE(mkfifo(pipe.c_str(), 0600) == 0);
    const auto pipeOnly = nesso::parser::expandInputs(std::vector<std::filesystem::path>{pipe});
    REQUIRE_FALSE(pipeOnly);
    CHECK(pipeOnly.error().path == pipe);

    const auto expanded = nesso::parser::expandInputs(std::vector<std::filesystem::path>{root});
    REQUIRE(expanded);
    REQUIRE(expanded->size() == 5);
    CHECK(expanded->at(0).filename() == ".hidden.log");
    CHECK(expanded->at(1).filename() == "a.log");
    CHECK(expanded->at(2).filename() == "b.log");
    CHECK(expanded->at(3).filename() == "c.log");
    CHECK(static_cast<unsigned char>(expanded->at(4).filename().string()[0]) == 0xFF);
    for (const std::filesystem::path &path : *expanded)
    {
        CHECK(path.filename() != "pipe");
    }
}

TEST_CASE("expandInputs does not follow directory symlinks and keeps file symlinks", "[parser][walk]")
{
    const TempDir dir{"nesso-walk-links"};
    const auto root = dir.path() / "logs";
    writeBytes(root / "real" / "a.log", "from-real\n");
    std::filesystem::create_directory_symlink(root / "real", root / "linkdir");
    writeBytes(root / "target.log", "from-file\n");
    std::filesystem::create_symlink(root / "target.log", root / "link.log");
    std::filesystem::create_symlink(root / "missing.log", root / "dangling.log");

    const auto walked = nesso::parser::expandInputs(std::vector<std::filesystem::path>{root});
    REQUIRE_FALSE(walked);
    CHECK(walked.error().path.filename() == "dangling.log");

    std::filesystem::remove(root / "dangling.log");
    const auto expanded = nesso::parser::expandInputs(std::vector<std::filesystem::path>{root});
    REQUIRE(expanded);
    std::vector<std::string> names;
    for (const std::filesystem::path &path : *expanded)
    {
        names.push_back(path.filename().string());
        CHECK(path.string().find("linkdir") == std::string::npos);
    }
    CHECK(names == std::vector<std::string>{"link.log", "a.log", "target.log"});

    const auto throughLink = nesso::parser::expandInputs(std::vector<std::filesystem::path>{root / "linkdir"});
    REQUIRE(throughLink);
    REQUIRE(throughLink->size() == 1);
    CHECK(throughLink->front().filename() == "a.log");
    CHECK(throughLink->front().parent_path().filename() == "linkdir");

    CHECK(textsOf(root / "link.log") == std::vector<std::string>{"from-file"});
}

TEST_CASE("expandInputs reports a missing path", "[parser][walk]")
{
    const TempDir dir{"nesso-walk-missing"};
    const auto missing = dir.path() / "nope.log";
    const auto expanded = nesso::parser::expandInputs(std::vector<std::filesystem::path>{missing});
    REQUIRE_FALSE(expanded);
    CHECK(expanded.error().path == missing);

    const auto empty = dir.path() / "empty";
    std::filesystem::create_directory(empty);
    const auto walked = nesso::parser::expandInputs(std::vector<std::filesystem::path>{empty});
    REQUIRE(walked);
    CHECK(walked->empty());
}

TEST_CASE("readGzip inflates members, concatenation, and rejects a short stream", "[parser][gzip]")
{
    const TempDir dir{"nesso-gzip-reader"};
    const auto one = dir.path() / "one.gz";
    const auto concat = dir.path() / "concat.gz";
    const auto empty = dir.path() / "empty.gz";
    const auto truncated = dir.path() / "short.gz";
    writeBytes(one, unhex(GZIP_DATABASE));
    writeBytes(concat, unhex(GZIP_CONCAT));
    writeBytes(empty, unhex(GZIP_EMPTY));
    writeBytes(truncated, unhex(GZIP_DATABASE).substr(0, 12));

    const auto text = nesso::parser::readGzip(one);
    REQUIRE(text);
    CHECK(*text == "database connection refused\n");
    const auto joined = nesso::parser::readGzip(concat);
    REQUIRE(joined);
    CHECK(*joined == "hello world\n");
    const auto blank = nesso::parser::readGzip(empty);
    REQUIRE(blank);
    CHECK(blank->empty());
    const auto bad = nesso::parser::readGzip(truncated);
    REQUIRE_FALSE(bad);
    CHECK(bad.error() == nesso::parser::GzipError::Corrupt);
    const auto missing = nesso::parser::readGzip(dir.path() / "missing.gz");
    REQUIRE_FALSE(missing);
    CHECK(missing.error() == nesso::parser::GzipError::OpenFailure);
    CHECK(nesso::parser::isGzipPath(dir.path() / "App.LOG.GZ"));
    CHECK_FALSE(nesso::parser::isGzipPath(dir.path() / "app.gz.bak"));
}

TEST_CASE("Chunker reads gzip logs and json, and rejects binary or corrupt gzip", "[parser][gzip]")
{
    const TempDir dir{"nesso-gzip-chunk"};
    const auto log = dir.path() / "app.log.gz";
    const auto upper = dir.path() / "APP.LOG.GZ";
    const auto json = dir.path() / "events.json.gz";
    const auto jsonl = dir.path() / "events.jsonl.gz";
    const auto binary = dir.path() / "blob.log.gz";
    const auto corrupt = dir.path() / "bad.gz";
    const auto two = dir.path() / "two.log.gz";
    writeBytes(log, unhex(GZIP_DATABASE));
    writeBytes(upper, unhex(GZIP_DATABASE));
    writeBytes(json, unhex(GZIP_JSON));
    writeBytes(jsonl, unhex(GZIP_JSONL));
    writeBytes(binary, unhex(GZIP_BINARY));
    writeBytes(corrupt, std::string{"\x1f\x8b\x08\x00", 4});
    writeBytes(two, unhex(GZIP_TWO));
    writeBytes(dir.path() / "raw.log", "caf\xFF\n");

    CHECK(textsOf(log) == std::vector<std::string>{"database connection refused"});
    CHECK(textsOf(upper) == std::vector<std::string>{"database connection refused"});
    CHECK(textsOf(json) == std::vector<std::string>{"database connection refused"});
    CHECK(textsOf(jsonl) == std::vector<std::string>{"first", "second line"});
    CHECK(textsOf(dir.path() / "raw.log") == std::vector<std::string>{"caf\xFF"});

    const auto rejected = nesso::parser::Chunker::fromFile(binary);
    REQUIRE_FALSE(rejected);
    CHECK(rejected.error() == nesso::parser::ParseError::BinaryFile);
    const auto broken = nesso::parser::Chunker::fromFile(corrupt);
    REQUIRE_FALSE(broken);
    CHECK(broken.error() == nesso::parser::ParseError::CorruptGzip);

    const auto second = nesso::parser::Chunker::readRecord(two, 4, 3, 2, false, "message", 4096);
    REQUIRE(second);
    CHECK(*second == "two");
    const auto object = nesso::parser::Chunker::readRecord(json, 0, 0, 1, false, "message", 4096);
    REQUIRE(object);
    CHECK(*object == "database connection refused");
}

TEST_CASE("an empty gzip file has no chunks", "[parser][gzip]")
{
    const TempDir dir{"nesso-gzip-empty"};
    const auto path = dir.path() / "empty.gz";
    writeBytes(path, unhex(GZIP_EMPTY));
    const auto chunks = nesso::parser::Chunker::fromFile(path);
    REQUIRE(chunks);
    CHECK(chunks->empty());
}

TEST_CASE("Chunker maps missing gzip to FileOpenFailure", "[parser][gzip]")
{
    const TempDir dir{"nesso-gzip-missing-chunker"};
    const auto missing = dir.path() / "missing.gz";
    const auto fromFile = nesso::parser::Chunker::fromFile(missing);
    REQUIRE_FALSE(fromFile);
    CHECK(fromFile.error() == nesso::parser::ParseError::FileOpenFailure);

    const auto walked =
        nesso::parser::Chunker::forEachRecord(missing, 4096, nullptr, "message", [](nesso::parser::ParsedChunk) {});
    REQUIRE_FALSE(walked);
    CHECK(walked.error() == nesso::parser::ParseError::FileOpenFailure);

    const auto record = nesso::parser::Chunker::readRecord(missing, 0, 1, 1, false, "message", 4096);
    REQUIRE_FALSE(record);
    CHECK(record.error() == nesso::parser::ParseError::FileOpenFailure);
}

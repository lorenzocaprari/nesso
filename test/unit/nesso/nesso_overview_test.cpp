// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_all.hpp>
#include <nesso/nesso.hpp>
#include <nesso/overview.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <unistd.h>
#include <vector>

namespace
{

class TempDir
{
  public:
    explicit TempDir(const std::string &name)
        : path_(std::filesystem::temp_directory_path() / (name + "_" + std::to_string(getpid())))
    {
        std::filesystem::remove_all(path_);
        std::filesystem::create_directories(path_);
    }

    TempDir(const TempDir &) = delete;
    auto operator=(const TempDir &) -> TempDir & = delete;
    TempDir(TempDir &&) = delete;
    auto operator=(TempDir &&) -> TempDir & = delete;
    ~TempDir() { std::filesystem::remove_all(path_); }

    [[nodiscard]] auto Path() const -> const std::filesystem::path & { return path_; }

  private:
    std::filesystem::path path_;
};

class CinGuard
{
  public:
    explicit CinGuard(std::istream &input) : previous_(std::cin.rdbuf(input.rdbuf())) { std::cin.clear(); }

    CinGuard(const CinGuard &) = delete;
    auto operator=(const CinGuard &) -> CinGuard & = delete;
    CinGuard(CinGuard &&) = delete;
    auto operator=(CinGuard &&) -> CinGuard & = delete;

    ~CinGuard()
    {
        std::cin.rdbuf(previous_);
        std::cin.clear();
    }

  private:
    std::streambuf *previous_;
};

void WriteFile(const std::filesystem::path &path, std::string_view text)
{
    std::ofstream output(path, std::ios::binary);
    REQUIRE(output);
    output << text;
    REQUIRE(output);
}

struct TarMember
{
    std::string name;
    std::string body;
    char type = '0';
};

void PutOctal(std::string &header, std::size_t offset, std::size_t width, std::uint64_t value)
{
    // A tar field is width - 1 octal digits and a NUL. Pad by hand: a dynamic
    // width in the format string does not compile under Clang.
    std::string digits = std::format("{:o}", value);
    digits.insert(0, width - 1 - digits.size(), '0');
    header.replace(offset, digits.size(), digits);
}

// A ustar archive: one 512-byte header per member, the body padded to 512
// bytes, then two zero blocks.
void WriteTar(const std::filesystem::path &path, const std::vector<TarMember> &members)
{
    std::string archive;
    for (const TarMember &member : members)
    {
        std::string header(512, '\0');
        header.replace(0, member.name.size(), member.name);
        PutOctal(header, 100, 8, 0644);
        PutOctal(header, 108, 8, 0);
        PutOctal(header, 116, 8, 0);
        PutOctal(header, 124, 12, member.body.size());
        PutOctal(header, 136, 12, 0);
        header[156] = member.type;
        header.replace(257, 6, std::string_view{"ustar\0", 6});
        header.replace(263, 2, "00");
        header.replace(148, 8, std::string(8, ' '));
        std::uint64_t sum = 0;
        for (const char byte : header)
        {
            sum += static_cast<unsigned char>(byte);
        }
        PutOctal(header, 148, 7, sum);
        archive += header;
        archive += member.body;
        archive.append((512 - member.body.size() % 512) % 512, '\0');
    }
    archive.append(1024, '\0');
    WriteFile(path, archive);
}

auto Request(std::vector<std::filesystem::path> files) -> nesso::OverviewRequest
{
    return nesso::OverviewRequest{.files = std::move(files),
                                  .top_k = nesso::DEFAULT_OVERVIEW_TOP_K,
                                  .json_field = "message",
                                  .order = nesso::EventOrder::Rare,
                                  .archive_limits = {},
                                  .max_types = nesso::DEFAULT_MAX_EVENT_TYPES};
}

auto Texts(const nesso::OverviewResults &results) -> std::vector<std::string>
{
    std::vector<std::string> texts;
    texts.reserve(results.rows.size());
    for (const nesso::EventTypeRow &row : results.rows)
    {
        texts.push_back(row.text);
    }
    return texts;
}

} // namespace

TEST_CASE("overview counts masked types across files in argument order", "[overview]")
{
    const TempDir dir{"nesso_overview_files"};
    const auto older = dir.Path() / "older.log";
    const auto newer = dir.Path() / "newer.log";
    WriteFile(older, "PacketResponder 1 for block blk_1 terminating\n"
                     "disk sda is healthy\n"
                     "PacketResponder 0 for block blk_2 terminating\n");
    WriteFile(newer, "PacketResponder 2 for block blk_3 terminating\n"
                     "kernel panic on dn228\n"
                     "\n"
                     "disk sda is healthy\n");

    nesso::OverviewRequest request = Request({older, newer});
    const auto rare = nesso::Overview(request);
    REQUIRE(rare);
    CHECK(rare->lines == 6);
    CHECK(rare->types == 3);
    CHECK(rare->empty_lines == 1);
    CHECK(rare->skipped_lines == 1);
    REQUIRE(rare->rows.size() == 3);
    CHECK(Texts(*rare) == std::vector<std::string>{"kernel panic on dn228", "disk sda is healthy",
                                                   "PacketResponder 1 for block blk_1 terminating"});

    const nesso::EventTypeRow &panic = rare->rows[0];
    CHECK(panic.count == 1);
    CHECK(panic.source == newer.string());
    CHECK(panic.line == 2);
    CHECK_FALSE(panic.array_element);
    CHECK(panic.key == "kernel panic on <*>");
    CHECK(panic.first_seen == 5);
    CHECK(panic.last_seen == 5);
    const nesso::EventTypeRow &disk = rare->rows[1];
    CHECK(disk.count == 2);
    CHECK(disk.source == older.string());
    CHECK(disk.line == 2);
    CHECK(disk.first_seen == 2);
    CHECK(disk.last_seen == 6);
    CHECK(rare->rows[2].count == 3);
    CHECK(rare->rows[2].key == "PacketResponder <*> for block <*> terminating");

    request.order = nesso::EventOrder::New;
    request.top_k = 2;
    const auto fresh = nesso::Overview(request);
    REQUIRE(fresh);
    CHECK(fresh->types == 3);
    CHECK(Texts(*fresh) == std::vector<std::string>{"kernel panic on dn228", "disk sda is healthy"});

    // The newest file first makes the other type the newest one.
    request.files = {newer, older};
    const auto swapped = nesso::Overview(request);
    REQUIRE(swapped);
    CHECK(Texts(*swapped) == std::vector<std::string>{"disk sda is healthy", "kernel panic on dn228"});
    CHECK(swapped->rows[0].source == newer.string());
    CHECK(swapped->rows[0].line == 4);
}

TEST_CASE("overview reads a directory in byte order and stdin once", "[overview]")
{
    const TempDir dir{"nesso_overview_dir"};
    std::filesystem::create_directories(dir.Path() / "logs");
    WriteFile(dir.Path() / "logs" / "b.log", "beta event\n");
    WriteFile(dir.Path() / "logs" / "a.log", "alpha event\nbeta event\n");

    const auto results = nesso::Overview(Request({dir.Path() / "logs"}));
    REQUIRE(results);
    REQUIRE(results->rows.size() == 2);
    CHECK(results->rows[0].text == "alpha event");
    CHECK(results->rows[0].source == (dir.Path() / "logs" / "a.log").string());
    CHECK(results->rows[1].count == 2);
    CHECK(results->rows[1].source == (dir.Path() / "logs" / "a.log").string());

    std::istringstream input{"one\ntwo\none\n"};
    const CinGuard guard{input};
    const auto piped = nesso::Overview(Request({"-", "-"}));
    REQUIRE(piped);
    CHECK(piped->lines == 3);
    REQUIRE(piped->rows.size() == 2);
    CHECK(piped->rows[0].source == "-");
    CHECK(piped->rows[0].text == "two");
}

TEST_CASE("overview cites JSON array elements and counts cut lines", "[overview]")
{
    const TempDir dir{"nesso_overview_json"};
    const auto rows = dir.Path() / "rows.json";
    WriteFile(rows, R"([{"message":"user 1 logged in"},{"other":"x"},{"message":"user 2 logged in"},)"
                    R"({"message":"quota exceeded"}])");
    const auto wide = dir.Path() / "wide.log";
    WriteFile(wide, std::string(5000, 'x') + "\n" + std::string(4100, 'x') + "\n");

    const auto results = nesso::Overview(Request({rows, wide}));
    REQUIRE(results);
    CHECK(results->missing_field_lines == 1);
    CHECK(results->truncated_lines == 2);
    CHECK(results->lines == 5);
    REQUIRE(results->rows.size() == 3);
    CHECK(results->rows[0].text == "quota exceeded");
    CHECK(results->rows[0].array_element);
    CHECK(results->rows[0].line == 4);
    // Both wide lines are cut to the same 4096 code points, so they are one type.
    CHECK(results->rows[1].source == wide.string());
    CHECK(results->rows[1].count == 2);
    CHECK(results->rows[1].text.size() == 4096);
    CHECK(results->rows[2].key == "user <*> logged in");
    CHECK(results->rows[2].line == 1);
    CHECK(results->rows[2].count == 2);

    nesso::OverviewRequest field = Request({rows});
    field.json_field = "other";
    const auto other = nesso::Overview(field);
    REQUIRE(other);
    CHECK(other->json_field == "other");
    CHECK(other->lines == 1);
    CHECK(other->missing_field_lines == 3);
}

TEST_CASE("overview labels archive members and counts skipped members", "[overview]")
{
    const TempDir dir{"nesso_overview_tar"};
    const auto bundle = dir.Path() / "bundle.tar";
    WriteTar(bundle, {{.name = "var/", .body = {}, .type = '5'},
                      {.name = "var/app.log", .body = "boot ok\nboot ok\ndisk failed\n", .type = '0'}});

    const auto results = nesso::Overview(Request({bundle}));
    REQUIRE(results);
    REQUIRE(results->rows.size() == 2);
    CHECK(results->rows[0].source == bundle.string() + ":var/app.log");
    CHECK(results->rows[0].text == "disk failed");
    CHECK(results->rows[0].line == 3);
    REQUIRE(results->bundle_skips.size() == 1);
    CHECK(results->bundle_skips[0].directories == 1);

    nesso::OverviewRequest limited = Request({bundle});
    limited.archive_limits.max_entry_bytes = 4;
    const auto failed = nesso::Overview(limited);
    REQUIRE_FALSE(failed);
    CHECK(failed.error().kind == nesso::ErrorKind::Parse);
    CHECK(failed.error().cause == nesso::ErrorCause::ArchiveLimit);
    CHECK(failed.error().path == bundle);
}

TEST_CASE("overview reports empty, missing, and binary inputs", "[overview]")
{
    const TempDir dir{"nesso_overview_errors"};
    const auto empty = dir.Path() / "empty.log";
    WriteFile(empty, "");
    const auto blank = dir.Path() / "blank.log";
    WriteFile(blank, "\n   \n");

    const auto nothing = nesso::Overview(Request({empty}));
    REQUIRE_FALSE(nothing);
    CHECK(nothing.error().kind == nesso::ErrorKind::EmptyInput);
    CHECK(nothing.error().cause == nesso::ErrorCause::EmptyFile);
    CHECK(nothing.error().path == empty);

    const auto skipped = nesso::Overview(Request({empty, blank}));
    REQUIRE_FALSE(skipped);
    CHECK(skipped.error().kind == nesso::ErrorKind::EmptyInput);
    CHECK(skipped.error().cause == nesso::ErrorCause::NothingIndexable);
    CHECK(skipped.error().path.empty());
    CHECK(skipped.error().empty_lines == 2);

    const auto missing = nesso::Overview(Request({dir.Path() / "missing.log"}));
    REQUIRE_FALSE(missing);
    CHECK(missing.error().kind == nesso::ErrorKind::Parse);
    CHECK(missing.error().cause == nesso::ErrorCause::FileOpen);

    const auto binary = dir.Path() / "binary.log";
    WriteFile(binary, std::string_view{"text\0more\n", 10});
    const auto rejected = nesso::Overview(Request({binary}));
    REQUIRE_FALSE(rejected);
    CHECK(rejected.error().kind == nesso::ErrorKind::Parse);
    CHECK(rejected.error().cause == nesso::ErrorCause::BinaryFile);
}

TEST_CASE("overview fails past max_types", "[overview]")
{
    const TempDir dir{"nesso_overview_cap"};
    const auto first = dir.Path() / "first.log";
    WriteFile(first, "alpha\nbeta\nalpha\n");
    const auto second = dir.Path() / "second.log";
    WriteFile(second, "beta\ngamma\ndelta\n");

    nesso::OverviewRequest request = Request({first, second});
    request.max_types = 2;
    const auto failed = nesso::Overview(request);
    REQUIRE_FALSE(failed);
    CHECK(failed.error().kind == nesso::ErrorKind::Parse);
    CHECK(failed.error().cause == nesso::ErrorCause::TooManyEventTypes);
    CHECK(failed.error().path == second);
    CHECK(nesso::Message(failed.error()) ==
          std::format("Error: failed to list the event types of '{}': more than 2 distinct event types",
                      second.string()));

    request.max_types = 4;
    const auto fits = nesso::Overview(request);
    REQUIRE(fits);
    CHECK(fits->types == 4);
}

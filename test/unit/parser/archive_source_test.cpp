// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_all.hpp>
#include <parser/archive_source.hpp>
#include <parser/gzip_reader.hpp>
#include <parser/input_walk.hpp>
#include <parser/log_chunker.hpp>

#include <archive.h>
#include <archive_entry.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <unistd.h>
#include <utility>
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

    [[nodiscard]] const std::filesystem::path &Path() const { return path_; }

  private:
    std::filesystem::path path_;
};

struct Member
{
    std::string name;
    std::string bytes;
    unsigned type = AE_IFREG;
    const char *link = nullptr;
};

struct WriteFree
{
    void operator()(archive *handle) const noexcept
    {
        if (handle != nullptr)
        {
            archive_write_free(handle);
        }
    }
};

struct EntryFree
{
    void operator()(archive_entry *entry) const noexcept
    {
        if (entry != nullptr)
        {
            archive_entry_free(entry);
        }
    }
};

void WriteBundle(const std::filesystem::path &dest, std::string_view format, std::string_view filter,
                 const std::vector<Member> &members)
{
    std::unique_ptr<archive, WriteFree> writer{archive_write_new()};
    REQUIRE(writer);
    if (format == "zip")
    {
        REQUIRE(archive_write_set_format_zip(writer.get()) == ARCHIVE_OK);
    }
    else
    {
        REQUIRE(archive_write_set_format_pax(writer.get()) == ARCHIVE_OK);
    }
    if (filter == "gzip")
    {
        REQUIRE(archive_write_add_filter_gzip(writer.get()) == ARCHIVE_OK);
    }
    else if (filter == "xz")
    {
        REQUIRE(archive_write_add_filter_xz(writer.get()) == ARCHIVE_OK);
    }
    else
    {
        REQUIRE(archive_write_add_filter_none(writer.get()) == ARCHIVE_OK);
    }
    REQUIRE(archive_write_open_filename(writer.get(), dest.c_str()) == ARCHIVE_OK);
    for (const Member &member : members)
    {
        std::unique_ptr<archive_entry, EntryFree> entry{archive_entry_new()};
        REQUIRE(entry);
        archive_entry_set_pathname(entry.get(), member.name.c_str());
        archive_entry_set_filetype(entry.get(), member.type);
        archive_entry_set_perm(entry.get(), member.type == AE_IFDIR ? 0755 : 0644);
        if (member.link != nullptr && member.type == AE_IFLNK)
        {
            archive_entry_set_symlink(entry.get(), member.link);
        }
        if (member.link != nullptr && member.type == AE_IFREG)
        {
            archive_entry_set_hardlink(entry.get(), member.link);
        }
        archive_entry_set_size(entry.get(), static_cast<la_int64_t>(member.bytes.size()));
        const int header_status = archive_write_header(writer.get(), entry.get());
        REQUIRE((header_status == ARCHIVE_OK || header_status == ARCHIVE_WARN));
        if (!member.bytes.empty() && member.link == nullptr)
        {
            const la_ssize_t wrote = archive_write_data(writer.get(), member.bytes.data(), member.bytes.size());
            REQUIRE(wrote == static_cast<la_ssize_t>(member.bytes.size()));
        }
        REQUIRE(archive_write_finish_entry(writer.get()) == ARCHIVE_OK);
    }
    REQUIRE(archive_write_close(writer.get()) == ARCHIVE_OK);
}

std::vector<std::pair<std::string, std::string>> ReadMembers(const std::filesystem::path &path,
                                                             const nesso::parser::ArchiveLimits &limits = {})
{
    std::vector<std::pair<std::string, std::string>> members;
    const auto read = nesso::parser::ReadArchive(path, limits, [&members](std::string_view name, std::string_view bytes)
                                                 { members.emplace_back(name, bytes); });
    REQUIRE(read);
    return members;
}

std::string Unhex(std::string_view hex)
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

const std::string_view GZIP_DATABASE = "1f8b0800f722f26a02ff4b492c494c4a2c4e5548cecfcb4b4d2ec9cccf53284a4d2b2d4e4d"
                                       "e10200e756bf4f1c000000";

void WriteEncryptedZip(const std::filesystem::path &dest, std::string_view name, std::string_view payload)
{
    // Minimal stored zip with general-purpose bit 0 (encrypted). Payload is not
    // decrypted.
    const auto name_bytes = static_cast<std::uint16_t>(name.size());
    const auto size = static_cast<std::uint32_t>(payload.size());
    std::string bytes;
    const auto append32 = [&bytes](std::uint32_t value)
    {
        bytes.push_back(static_cast<char>(value & 0xffU));
        bytes.push_back(static_cast<char>((value >> 8) & 0xffU));
        bytes.push_back(static_cast<char>((value >> 16) & 0xffU));
        bytes.push_back(static_cast<char>((value >> 24) & 0xffU));
    };
    const auto append16 = [&bytes](std::uint16_t value)
    {
        bytes.push_back(static_cast<char>(value & 0xffU));
        bytes.push_back(static_cast<char>((value >> 8) & 0xffU));
    };
    append32(0x04034b50U);
    append16(20);
    append16(0x0001);
    append16(0);
    append16(0);
    append16(0);
    append32(0);
    append32(size);
    append32(size);
    append16(name_bytes);
    append16(0);
    bytes.append(name);
    bytes.append(payload);
    const auto local_size = static_cast<std::uint32_t>(bytes.size());
    append32(0x02014b50U);
    append16(20);
    append16(20);
    append16(0x0001);
    append16(0);
    append16(0);
    append16(0);
    append32(0);
    append32(size);
    append32(size);
    append16(name_bytes);
    append16(0);
    append16(0);
    append16(0);
    append16(0);
    append32(0);
    append32(0);
    bytes.append(name);
    const auto central_size = static_cast<std::uint32_t>(bytes.size()) - local_size;
    append32(0x06054b50U);
    append16(0);
    append16(0);
    append16(1);
    append16(1);
    append32(central_size);
    append32(local_size);
    append16(0);
    std::ofstream output(dest, std::ios::binary);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

} // namespace

TEST_CASE("LooksLikeOpenPgp recognizes armor and packet tags", "[parser][archive][openpgp]")
{
    const auto as_bytes = [](std::string_view text)
    { return std::span<const std::byte>{reinterpret_cast<const std::byte *>(text.data()), text.size()}; };
    CHECK(nesso::parser::LooksLikeOpenPgp(as_bytes("-----BEGIN PGP MESSAGE-----\n")));
    CHECK(nesso::parser::LooksLikeOpenPgp(as_bytes(std::string{static_cast<char>(0x84)}))); // old PKESK
    CHECK(nesso::parser::LooksLikeOpenPgp(as_bytes(std::string{static_cast<char>(0x8C)}))); // old SKESK
    CHECK(nesso::parser::LooksLikeOpenPgp(as_bytes(std::string{static_cast<char>(0xC1)}))); // new PKESK
    CHECK(nesso::parser::LooksLikeOpenPgp(as_bytes(std::string{static_cast<char>(0xC3)}))); // new SKESK
    CHECK(nesso::parser::LooksLikeOpenPgp(as_bytes(std::string{static_cast<char>(0xD2)}))); // new SEIPD
    CHECK_FALSE(nesso::parser::LooksLikeOpenPgp(as_bytes("this is not a tar")));
    CHECK_FALSE(nesso::parser::LooksLikeOpenPgp(as_bytes(std::string{static_cast<char>(0xFD)}))); // xz magic-ish
    CHECK_FALSE(nesso::parser::LooksLikeOpenPgp({}));
}

TEST_CASE("archive paths are recognized without regard to case", "[parser][archive]")
{
    CHECK(nesso::parser::IsArchivePath("bundle.tar"));
    CHECK(nesso::parser::IsArchivePath("bundle.TGZ"));
    CHECK(nesso::parser::IsArchivePath("bundle.TAR.GZ"));
    CHECK(nesso::parser::IsArchivePath("SOS.TAR.XZ"));
    CHECK(nesso::parser::IsArchivePath("bundle.zip"));
    CHECK_FALSE(nesso::parser::IsArchivePath("app.log.gz"));
    CHECK_FALSE(nesso::parser::IsArchivePath("notes.tar.bz2"));
    CHECK_FALSE(nesso::parser::IsArchivePath("plain.log"));
}

TEST_CASE("ReadGzipBytes inflates the same bytes as ReadGzip", "[parser][gzip]")
{
    const std::string bytes = Unhex(GZIP_DATABASE);
    const auto text = nesso::parser::ReadGzipBytes(bytes);
    REQUIRE(text);
    CHECK(*text == "database connection refused\n");
    const auto broken = nesso::parser::ReadGzipBytes(std::string_view{"\x1f\x8b\x08\x00", 4});
    REQUIRE_FALSE(broken);
    CHECK(broken.error() == nesso::parser::GzipError::Corrupt);
}

TEST_CASE("archives are read in archive order and never touch member paths", "[parser][archive]")
{
    const TempDir dir{"nesso-archive-order"};
    const std::vector<Member> members{{.name = "b.log", .bytes = "second line\n"},
                                      {.name = "./var/log/syslog", .bytes = "database connection refused\n"},
                                      {.name = "var\\log\\auth.log", .bytes = "authentication failure\n"},
                                      {.name = "/etc/passwd", .bytes = "root entry\n"},
                                      {.name = "../secret", .bytes = "not a path\n"},
                                      {.name = "nested", .bytes = {}, .type = AE_IFDIR},
                                      {.name = "var/log/link", .bytes = {}, .type = AE_IFLNK, .link = "syslog"},
                                      {.name = "b-link", .bytes = {}, .type = AE_IFREG, .link = "b.log"}};
    for (const char *format : {"tar", "zip"})
    {
        // Zip has no hardlink record. libarchive writes that member as an empty
        // regular file.
        std::vector<Member> written = members;
        if (std::string_view{format} == "zip")
        {
            std::erase_if(written,
                          [](const Member &member) { return member.type == AE_IFREG && member.link != nullptr; });
        }
        const auto path = dir.Path() / (std::string{"bundle."} + format);
        WriteBundle(path, format, "none", written);
        const auto read = ReadMembers(path);
        REQUIRE(read.size() == 5);
        CHECK(read[0] == std::pair<std::string, std::string>{"b.log", "second line\n"});
        CHECK(read[1] == std::pair<std::string, std::string>{"var/log/syslog", "database connection refused\n"});
        CHECK(read[2] == std::pair<std::string, std::string>{"var/log/auth.log", "authentication failure\n"});
        CHECK(read[3] == std::pair<std::string, std::string>{"/etc/passwd", "root entry\n"});
        CHECK(read[4] == std::pair<std::string, std::string>{"../secret", "not a path\n"});
    }
    CHECK_FALSE(std::filesystem::exists(dir.Path() / "etc" / "passwd"));
    CHECK_FALSE(std::filesystem::exists(dir.Path() / "secret"));
    CHECK_FALSE(std::filesystem::exists(dir.Path() / "b.log"));
    CHECK(std::filesystem::is_regular_file(dir.Path() / "bundle.tar"));
    CHECK(std::filesystem::is_regular_file(dir.Path() / "bundle.zip"));
}

TEST_CASE("tar gz tgz and tar xz carry the same members", "[parser][archive]")
{
    const TempDir dir{"nesso-archive-compressed"};
    const std::vector<Member> members{{.name = "var/log/messages", .bytes = "database connection refused\n"}};
    WriteBundle(dir.Path() / "bundle.tar.gz", "tar", "gzip", members);
    WriteBundle(dir.Path() / "bundle.tgz", "tar", "gzip", members);
    WriteBundle(dir.Path() / "bundle.tar.xz", "tar", "xz", members);
    for (const char *name : {"bundle.tar.gz", "bundle.tgz", "bundle.tar.xz"})
    {
        const auto read = ReadMembers(dir.Path() / name);
        REQUIRE(read.size() == 1);
        CHECK(read[0].first == "var/log/messages");
        CHECK(read[0].second == "database connection refused\n");
    }
}

TEST_CASE("archive limits name the cap they hit", "[parser][archive]")
{
    const TempDir dir{"nesso-archive-limits"};
    const auto path = dir.Path() / "bundle.tar";
    WriteBundle(path, "tar", "none",
                {{.name = "a.log", .bytes = std::string(30, 'a')},
                 {.name = "b.log", .bytes = std::string(30, 'b')},
                 {.name = "c.log", .bytes = std::string(30, 'c')}});

    nesso::parser::ArchiveLimits entry_limit;
    entry_limit.max_entry_bytes = 10;
    const auto too_big = nesso::parser::ReadArchive(path, entry_limit, [](std::string_view, std::string_view) {});
    REQUIRE_FALSE(too_big);
    CHECK(too_big.error().code == nesso::parser::ArchiveError::EntryTooLarge);

    nesso::parser::ArchiveLimits total_limit;
    total_limit.max_total_bytes = 40;
    const auto too_much = nesso::parser::ReadArchive(path, total_limit, [](std::string_view, std::string_view) {});
    REQUIRE_FALSE(too_much);
    CHECK(too_much.error().code == nesso::parser::ArchiveError::TotalTooLarge);

    nesso::parser::ArchiveLimits count_limit;
    count_limit.max_entries = 2;
    const auto too_many = nesso::parser::ReadArchive(path, count_limit, [](std::string_view, std::string_view) {});
    REQUIRE_FALSE(too_many);
    CHECK(too_many.error().code == nesso::parser::ArchiveError::TooManyEntries);

    const auto bomb = dir.Path() / "bomb.tar.xz";
    WriteBundle(bomb, "tar", "xz", {{.name = "pad.log", .bytes = std::string(20000, 'A')}});
    nesso::parser::ArchiveLimits ratio_limit;
    ratio_limit.max_ratio = 2;
    const auto too_dense = nesso::parser::ReadArchive(bomb, ratio_limit, [](std::string_view, std::string_view) {});
    REQUIRE_FALSE(too_dense);
    CHECK(too_dense.error().code == nesso::parser::ArchiveError::RatioExceeded);

    nesso::parser::ParseStats stats;
    const auto parsed = nesso::parser::Chunker::FromFile(path, 4096, &stats, "message", count_limit);
    REQUIRE_FALSE(parsed);
    CHECK(parsed.error() == nesso::parser::ParseError::ArchiveCountLimit);
}

TEST_CASE("a corrupt archive is rejected and an empty one has no members", "[parser][archive]")
{
    const TempDir dir{"nesso-archive-corrupt"};
    const auto bad = dir.Path() / "bad.tar";
    {
        std::ofstream output(bad, std::ios::binary);
        output << "this is not a tar";
    }
    const auto corrupt = nesso::parser::ReadArchive(bad, {}, [](std::string_view, std::string_view) {});
    REQUIRE_FALSE(corrupt);
    CHECK(corrupt.error().code == nesso::parser::ArchiveError::Corrupt);
    const auto chunks = nesso::parser::Chunker::FromFile(bad);
    REQUIRE_FALSE(chunks);
    CHECK(chunks.error() == nesso::parser::ParseError::CorruptArchive);

    const auto missing =
        nesso::parser::ReadArchive(dir.Path() / "missing.tar", {}, [](std::string_view, std::string_view) {});
    REQUIRE_FALSE(missing);
    CHECK(missing.error().code == nesso::parser::ArchiveError::OpenFailure);

    const auto empty = dir.Path() / "empty.zip";
    {
        std::ofstream output(empty, std::ios::binary);
    }
    const auto none = ReadMembers(empty);
    CHECK(none.empty());
    const auto empty_chunks = nesso::parser::Chunker::FromFile(empty);
    REQUIRE(empty_chunks);
    CHECK(empty_chunks->empty());

    const auto record = nesso::parser::Chunker::ReadRecord(
        bad, {.text = {}, .line_number = 1, .byte_offset = 0, .array_element = false, .byte_length = 1}, "message",
        4096);
    REQUIRE_FALSE(record);
    CHECK(record.error() == nesso::parser::ParseError::UnsupportedFormat);
}

TEST_CASE("Chunker labels archive lines and skips binary members", "[parser][archive]")
{
    const TempDir dir{"nesso-archive-chunk"};
    const auto path = dir.Path() / "logs" / "bundle.tgz";
    std::filesystem::create_directories(path.parent_path());
    const std::string gzip_log = Unhex(GZIP_DATABASE);
    WriteBundle(path, "tar", "gzip",
                {{.name = "b.log", .bytes = "beta\n"},
                 {.name = "a.log", .bytes = "alpha\n"},
                 {.name = "lib/mod.bin", .bytes = std::string("bin\0ary", 7)},
                 {.name = "events.json", .bytes = "{\"message\":\"database connection refused\"}\n"},
                 {.name = "var/log/messages.gz", .bytes = gzip_log}});

    nesso::parser::ParseStats stats;
    const auto chunks = nesso::parser::Chunker::FromFile(path, 4096, &stats);
    REQUIRE(chunks);
    REQUIRE(chunks->size() == 4);
    CHECK(stats.binary_entries == 1);
    CHECK((*chunks)[0].source == path.string() + ":b.log");
    CHECK((*chunks)[0].text == "beta");
    CHECK((*chunks)[0].entry_start);
    CHECK((*chunks)[1].source == path.string() + ":a.log");
    CHECK((*chunks)[1].text == "alpha");
    CHECK((*chunks)[2].text == "database connection refused");
    CHECK((*chunks)[2].source.ends_with(":events.json"));
    CHECK((*chunks)[3].text == "database connection refused");
    CHECK((*chunks)[3].source.ends_with(":var/log/messages.gz"));

    const auto walked = nesso::parser::ExpandInputs(std::vector<std::filesystem::path>{dir.Path() / "logs"});
    REQUIRE(walked);
    REQUIRE(walked->size() == 1);
    CHECK(walked->front() == path);

    const auto broken_gz = dir.Path() / "broken.tar";
    WriteBundle(broken_gz, "tar", "none", {{.name = "bad.gz", .bytes = std::string{"\x1f\x8b\x08\x00", 4}}});
    const auto rejected = nesso::parser::Chunker::FromFile(broken_gz);
    REQUIRE_FALSE(rejected);
    CHECK(rejected.error() == nesso::parser::ParseError::CorruptGzip);
}

TEST_CASE("encrypted zip entries and OpenPGP files are named", "[parser][archive]")
{
    const TempDir dir{"nesso-archive-encrypted"};
    const auto zip = dir.Path() / "secret.zip";
    WriteEncryptedZip(zip, "secret.log", "database connection refused\n");
    const auto encrypted = nesso::parser::ReadArchive(zip, {}, [](std::string_view, std::string_view) {});
    REQUIRE_FALSE(encrypted);
    CHECK(encrypted.error().code == nesso::parser::ArchiveError::Encrypted);
    CHECK(encrypted.error().entry == "secret.log");
    nesso::parser::ParseStats stats;
    const auto parsed = nesso::parser::Chunker::FromFile(zip, 4096, &stats);
    REQUIRE_FALSE(parsed);
    CHECK(parsed.error() == nesso::parser::ParseError::EncryptedArchive);
    CHECK(stats.encrypted_entry == "secret.log");

    const auto armored = dir.Path() / "sosreport-host.tar.xz";
    {
        std::ofstream output(armored, std::ios::binary);
        output << "-----BEGIN PGP MESSAGE-----\n\n";
    }
    const auto open_pgp = nesso::parser::ReadArchive(armored, {}, [](std::string_view, std::string_view) {});
    REQUIRE_FALSE(open_pgp);
    CHECK(open_pgp.error().code == nesso::parser::ArchiveError::Encrypted);
    CHECK(open_pgp.error().entry.empty());

    const auto skesk = dir.Path() / "bundle.tar";
    {
        std::ofstream output(skesk, std::ios::binary);
        output.put(static_cast<char>(0x8C));
        output.put(static_cast<char>(0x01));
        output.put(static_cast<char>(0x00));
    }
    const auto binary_pgp = nesso::parser::ReadArchive(skesk, {}, [](std::string_view, std::string_view) {});
    REQUIRE_FALSE(binary_pgp);
    CHECK(binary_pgp.error().code == nesso::parser::ArchiveError::Encrypted);

    const auto truncated = dir.Path() / "cut.tar.xz";
    WriteBundle(dir.Path() / "full.tar.xz", "tar", "xz", {{.name = "a.log", .bytes = "database connection refused\n"}});
    {
        std::ifstream input(dir.Path() / "full.tar.xz", std::ios::binary);
        std::string bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        REQUIRE(bytes.size() > 16);
        bytes.resize(bytes.size() / 2);
        std::ofstream output(truncated, std::ios::binary);
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }
    const auto corrupt = nesso::parser::ReadArchive(truncated, {}, [](std::string_view, std::string_view) {});
    REQUIRE_FALSE(corrupt);
    CHECK(corrupt.error().code == nesso::parser::ArchiveError::Corrupt);
}

TEST_CASE("archive skips count directories links and binary members", "[parser][archive]")
{
    const TempDir dir{"nesso-archive-skips"};
    const auto path = dir.Path() / "mix.tar";
    WriteBundle(path, "tar", "none",
                {{.name = "nested", .bytes = {}, .type = AE_IFDIR},
                 {.name = "var/log/link", .bytes = {}, .type = AE_IFLNK, .link = "syslog"},
                 {.name = "var/log/syslog", .bytes = "database connection refused\n"},
                 {.name = "lib/mod.bin", .bytes = std::string("bin\0ary", 7)}});
    nesso::parser::ArchiveSkipStats skips;
    const auto read = nesso::parser::ReadArchive(path, {}, [](std::string_view, std::string_view) {}, &skips);
    REQUIRE(read);
    CHECK(skips.directories == 1);
    CHECK(skips.symlinks == 1);
    CHECK(skips.hardlinks == 0);
    nesso::parser::ParseStats stats;
    const auto chunks = nesso::parser::Chunker::FromFile(path, 4096, &stats);
    REQUIRE(chunks);
    REQUIRE(chunks->size() == 1);
    CHECK(stats.binary_entries == 1);
    CHECK(stats.archive_directories == 1);
    CHECK(stats.archive_symlinks == 1);
}

TEST_CASE("archive hardlinks increment skip stats", "[parser][archive]")
{
    const TempDir dir{"nesso-archive-hardlink"};
    const auto path = dir.Path() / "links.tar";
    WriteBundle(path, "tar", "none",
                {{.name = "var/log/syslog", .bytes = "database connection refused\n"},
                 {.name = "var/log/messages", .bytes = {}, .type = AE_IFREG, .link = "var/log/syslog"}});
    nesso::parser::ArchiveSkipStats skips;
    const auto members = [&]
    {
        std::vector<std::pair<std::string, std::string>> out;
        const auto read = nesso::parser::ReadArchive(
            path, {}, [&out](std::string_view name, std::string_view bytes) { out.emplace_back(name, bytes); }, &skips);
        REQUIRE(read);
        return out;
    }();
    REQUIRE(members.size() == 1);
    CHECK(members[0].first == "var/log/syslog");
    CHECK(skips.hardlinks == 1);
    CHECK(skips.directories == 0);
    CHECK(skips.symlinks == 0);
}

TEST_CASE("nested gzip exceeding maxEntryBytes after inflate", "[parser][archive]")
{
    const TempDir dir{"nesso-archive-nested-gzip-limit"};
    const auto path = dir.Path() / "nested.tar";
    const std::string gzip_log = Unhex(GZIP_DATABASE);
    WriteBundle(path, "tar", "none", {{.name = "var/log/messages.gz", .bytes = gzip_log}});

    nesso::parser::ArchiveLimits entry_limit;
    entry_limit.max_entry_bytes = 10;
    const auto too_big = nesso::parser::ReadArchive(path, entry_limit, [](std::string_view, std::string_view) {});
    REQUIRE_FALSE(too_big);
    CHECK(too_big.error().code == nesso::parser::ArchiveError::EntryTooLarge);

    nesso::parser::ArchiveLimits total_limit;
    total_limit.max_total_bytes = 10;
    const auto too_much = nesso::parser::ReadArchive(path, total_limit, [](std::string_view, std::string_view) {});
    REQUIRE_FALSE(too_much);
    CHECK(too_much.error().code == nesso::parser::ArchiveError::TotalTooLarge);
}

TEST_CASE("Chunker maps archive entry total and ratio limits", "[parser][archive]")
{
    const TempDir dir{"nesso-archive-chunker-limits"};
    const auto path = dir.Path() / "bundle.tar";
    WriteBundle(path, "tar", "none",
                {{.name = "a.log", .bytes = std::string(30, 'a')},
                 {.name = "b.log", .bytes = std::string(30, 'b')},
                 {.name = "c.log", .bytes = std::string(30, 'c')}});

    nesso::parser::ArchiveLimits entry_limit;
    entry_limit.max_entry_bytes = 10;
    const auto entry = nesso::parser::Chunker::FromFile(path, 4096, nullptr, "message", entry_limit);
    REQUIRE_FALSE(entry);
    CHECK(entry.error() == nesso::parser::ParseError::ArchiveEntryLimit);

    nesso::parser::ArchiveLimits total_limit;
    total_limit.max_total_bytes = 40;
    const auto total = nesso::parser::Chunker::FromFile(path, 4096, nullptr, "message", total_limit);
    REQUIRE_FALSE(total);
    CHECK(total.error() == nesso::parser::ParseError::ArchiveTotalLimit);

    const auto bomb = dir.Path() / "bomb.tar.xz";
    WriteBundle(bomb, "tar", "xz", {{.name = "pad.log", .bytes = std::string(20000, 'A')}});
    nesso::parser::ArchiveLimits ratio_limit;
    ratio_limit.max_ratio = 2;
    const auto ratio = nesso::parser::Chunker::FromFile(bomb, 4096, nullptr, "message", ratio_limit);
    REQUIRE_FALSE(ratio);
    CHECK(ratio.error() == nesso::parser::ParseError::ArchiveRatioLimit);
}

TEST_CASE("Chunker marks entryStart on a skipped first archive record", "[parser][archive]")
{
    const TempDir dir{"nesso-archive-entry-start-skip"};
    const auto path = dir.Path() / "blank-first.tar";
    WriteBundle(path, "tar", "none", {{.name = "app.log", .bytes = "\nkept\n"}});

    std::vector<nesso::parser::ParsedChunk> records;
    const auto walked = nesso::parser::Chunker::ForEachRecord(path, 4096, nullptr, "message",
                                                              [&records](nesso::parser::ParsedChunk chunk)
                                                              { records.push_back(std::move(chunk)); });
    REQUIRE(walked);
    REQUIRE(records.size() == 2);
    CHECK(records[0].skipped);
    CHECK(records[0].entry_start);
    CHECK(records[0].text.empty());
    CHECK_FALSE(records[1].skipped);
    CHECK_FALSE(records[1].entry_start);
    CHECK(records[1].text == "kept");
}

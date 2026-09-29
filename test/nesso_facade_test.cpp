// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#include <catch2/catch_all.hpp>
#include <nesso.hpp>

#include <array>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>
#include <utility>

#ifndef NESSO_TEST_FIXTURES
#error "NESSO_TEST_FIXTURES must be defined"
#endif

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
    TempDir &operator=(const TempDir &) = delete;
    TempDir(TempDir &&) = delete;
    TempDir &operator=(TempDir &&) = delete;

    ~TempDir() { std::filesystem::remove_all(path_); }

    [[nodiscard]] const std::filesystem::path &path() const { return path_; }

  private:
    std::filesystem::path path_;
};

void writeBytes(const std::filesystem::path &path, const void *data, size_t size)
{
    std::ofstream output(path, std::ios::binary);
    REQUIRE(output);
    output.write(static_cast<const char *>(data), static_cast<std::streamsize>(size));
    REQUIRE(output);
}

void writeFloats(const std::filesystem::path &path, const std::array<float, 4> &values)
{
    writeBytes(path, values.data(), sizeof(values));
}

void writeEmptyCorpus(const std::filesystem::path &path)
{
    std::array<unsigned char, 24> header{};
    header[0] = 'N';
    header[1] = 'E';
    header[2] = 'S';
    header[3] = 'C';
    header[4] = 1;
    writeBytes(path, header.data(), header.size());
}

nesso::Nesso withMissingModel(const std::filesystem::path &root)
{
    return nesso::Nesso{nesso::Config{.modelDir = root / "no-models"}};
}

} // namespace

TEST_CASE("store init, ingest, and search round-trip", "[nesso][store]")
{
    const TempDir dir{"nesso_facade_store"};
    const auto db = dir.path() / "vectors.nesso";
    const auto vectors = dir.path() / "vectors.bin";
    writeFloats(vectors, {1.0F, 0.0F, 0.0F, 0.0F});

    const nesso::Nesso app{nesso::Config{}};

    const auto created = app.storeInit({.db = db, .dimensions = 4});
    REQUIRE(created.has_value());
    REQUIRE(created->dimensions == 4);

    const auto ingested = app.storeIngest({.db = db, .dimensions = 4, .input = vectors});
    REQUIRE(ingested.has_value());
    REQUIRE(ingested->before == 0);
    REQUIRE(ingested->added == 1);
    REQUIRE(ingested->total == 1);
    REQUIRE_FALSE(ingested->appendFailureCode.has_value());

    const auto matches = app.storeSearch({.db = db, .dimensions = 4, .query = vectors, .topK = 1});
    REQUIRE(matches.has_value());
    REQUIRE(matches->size() == 1);
    REQUIRE((*matches)[0].index == 0);
    REQUIRE((*matches)[0].score == Catch::Approx(1.0F));
}

TEST_CASE("store failures map to error kinds", "[nesso][store]")
{
    const TempDir dir{"nesso_facade_store_errors"};
    const auto db = dir.path() / "vectors.nesso";
    const auto shortQuery = dir.path() / "short.bin";
    const std::array<char, 4> shortBytes{};
    writeBytes(shortQuery, shortBytes.data(), shortBytes.size());

    const nesso::Nesso app{nesso::Config{}};

    const auto zeroDims = app.storeInit({.db = db, .dimensions = 0});
    REQUIRE_FALSE(zeroDims.has_value());
    REQUIRE(zeroDims.error().kind == nesso::ErrorKind::StoreInit);

    const auto missing = app.storeSearch({.db = db, .dimensions = 4, .query = shortQuery});
    REQUIRE_FALSE(missing.has_value());
    REQUIRE(missing.error().kind == nesso::ErrorKind::StoreMissing);
    REQUIRE(missing.error().path == db);

    REQUIRE(app.storeInit({.db = db, .dimensions = 4}).has_value());

    const auto mismatch = app.storeIngest({.db = db, .dimensions = 8, .input = shortQuery});
    REQUIRE_FALSE(mismatch.has_value());
    REQUIRE(mismatch.error().kind == nesso::ErrorKind::StoreOpen);

    const auto noInput = app.storeIngest({.db = db, .dimensions = 4, .input = dir.path() / "absent.bin"});
    REQUIRE_FALSE(noInput.has_value());
    REQUIRE(noInput.error().kind == nesso::ErrorKind::InputOpen);

    const auto wrongSize = app.storeSearch({.db = db, .dimensions = 4, .query = shortQuery});
    REQUIRE_FALSE(wrongSize.has_value());
    REQUIRE(wrongSize.error().kind == nesso::ErrorKind::QuerySize);
    REQUIRE(wrongSize.error().dimensions == 4);
}

TEST_CASE("text operations report model and corpus failures", "[nesso][text]")
{
    const TempDir dir{"nesso_facade_text"};
    const auto log = std::filesystem::path(NESSO_TEST_FIXTURES) / "grep_sample.log";
    const auto corrupt = dir.path() / "corrupt.nesso";
    const auto empty = dir.path() / "empty.nesso";
    const std::array<char, 4> junk{'b', 'a', 'd', '!'};
    writeBytes(corrupt, junk.data(), junk.size());
    writeEmptyCorpus(empty);

    const nesso::Nesso app = withMissingModel(dir.path());

    const auto grepped = app.grep({.query = "database", .files = {log}});
    REQUIRE_FALSE(grepped.has_value());
    REQUIRE(grepped.error().kind == nesso::ErrorKind::EmbedderLoad);
    REQUIRE(grepped.error().path == dir.path() / "no-models");

    const auto indexed = app.index({.files = {log}, .output = dir.path() / "out.nesso"});
    REQUIRE_FALSE(indexed.has_value());
    REQUIRE(indexed.error().kind == nesso::ErrorKind::EmbedderLoad);

    const auto unreadable = app.search({.query = "database", .index = corrupt});
    REQUIRE_FALSE(unreadable.has_value());
    REQUIRE(unreadable.error().kind == nesso::ErrorKind::CorpusRead);
    REQUIRE(unreadable.error().path == corrupt);

    const auto nothing = app.search({.query = "database", .index = empty});
    REQUIRE_FALSE(nothing.has_value());
    REQUIRE(nothing.error().kind == nesso::ErrorKind::NoMatches);
}

TEST_CASE("Nesso is movable", "[nesso]")
{
    const TempDir dir{"nesso_facade_move"};
    nesso::Nesso first{nesso::Config{}};
    nesso::Nesso second = std::move(first);
    first = std::move(second);
    REQUIRE(first.storeInit({.db = dir.path() / "moved.nesso", .dimensions = 4}).has_value());
}

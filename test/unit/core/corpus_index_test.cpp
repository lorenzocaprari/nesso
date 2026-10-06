// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#include <catch2/catch_all.hpp>
#include <core/corpus_index.hpp>

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>
#include <vector>

namespace
{

#pragma pack(push, 1)
struct RawHeader
{
    std::array<uint8_t, 4> magic{'N', 'E', 'S', 'C'};
    uint32_t version = 1;
    uint64_t dimensions = 0;
    uint64_t chunkCount = 0;
};
#pragma pack(pop)

std::filesystem::path makeTempDir()
{
    static int counter = 0;
    const auto path = std::filesystem::temp_directory_path() /
                      ("nesso_corpus_" + std::to_string(getpid()) + "_" + std::to_string(counter++));
    std::filesystem::remove_all(path);
    std::filesystem::create_directory(path);
    return path;
}

void writeRaw(const std::filesystem::path &path, const RawHeader &header, std::string_view tail = {})
{
    std::ofstream output(path, std::ios::binary);
    REQUIRE(output);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    output.write(reinterpret_cast<const char *>(&header), static_cast<std::streamsize>(sizeof(header)));
    output.write(tail.data(), static_cast<std::streamsize>(tail.size()));
    REQUIRE(output);
}

} // namespace

TEST_CASE("corpus file round-trips chunks", "[corpus][core]")
{
    const auto directory = makeTempDir();
    const auto path = directory / "corpus.nesso";

    const std::vector<core::CorpusChunk> chunks{
        {.chunk = {.text = "alpha", .lineNumber = 1, .source = "a.log"}, .embedding = {1.F, 0.F}},
        {.chunk = {.text = "beta", .lineNumber = 4, .source = "b.log"}, .embedding = {0.F, 1.F}},
    };
    REQUIRE(core::writeCorpusFile(path, chunks));

    const auto read = core::readCorpusFile(path);
    REQUIRE(read);
    REQUIRE(read->size() == 2);
    REQUIRE((*read)[0].chunk.text == "alpha");
    REQUIRE((*read)[0].chunk.lineNumber == 1);
    REQUIRE((*read)[0].chunk.source == "a.log");
    REQUIRE((*read)[0].embedding.size() == 2);
    REQUIRE((*read)[0].embedding[0] == 1.F);
    REQUIRE((*read)[1].chunk.text == "beta");
    REQUIRE((*read)[1].embedding[1] == 1.F);

    std::filesystem::remove_all(directory);
}

TEST_CASE("empty corpus is a valid file", "[corpus][core]")
{
    const auto directory = makeTempDir();
    const auto path = directory / "empty.nesso";
    REQUIRE(core::writeCorpusFile(path, {}));
    const auto read = core::readCorpusFile(path);
    REQUIRE(read);
    REQUIRE(read->empty());
    std::filesystem::remove_all(directory);
}

TEST_CASE("corpus writer rejects bad embeddings", "[corpus][core]")
{
    const auto directory = makeTempDir();
    const auto path = directory / "bad.nesso";

    const core::CorpusChunk emptyEmbedding{.chunk = {.text = "alpha", .lineNumber = 1, .source = "a.log"},
                                           .embedding = {}};
    const auto empty = core::writeCorpusFile(path, std::span<const core::CorpusChunk>(&emptyEmbedding, 1));
    REQUIRE_FALSE(empty);
    REQUIRE(empty.error() == core::EngineError::DatabaseNotInitialized);

    const std::vector<core::CorpusChunk> mismatched{
        {.chunk = {.text = "alpha", .lineNumber = 1, .source = "a.log"}, .embedding = {1.F}},
        {.chunk = {.text = "beta", .lineNumber = 2, .source = "a.log"}, .embedding = {1.F, 2.F}},
    };
    const auto mismatch = core::writeCorpusFile(path, mismatched);
    REQUIRE_FALSE(mismatch);
    REQUIRE(mismatch.error() == core::EngineError::MismatchedDimensions);

    core::CorpusChunk huge{.chunk = {.text = "alpha", .lineNumber = 1, .source = "a.log"},
                           .embedding = std::vector<float>(65537, 1.F)};
    const auto oversized = core::writeCorpusFile(path, std::span<const core::CorpusChunk>(&huge, 1));
    REQUIRE_FALSE(oversized);
    REQUIRE(oversized.error() == core::EngineError::CorruptDatabase);

    core::CorpusChunk longText{
        .chunk = {.text = std::string(8ULL * 1024ULL * 1024ULL + 1, 'x'), .lineNumber = 1, .source = {}},
        .embedding = {1.F}};
    const auto longWrite = core::writeCorpusFile(path, std::span<const core::CorpusChunk>(&longText, 1));
    REQUIRE_FALSE(longWrite);
    REQUIRE(longWrite.error() == core::EngineError::CorruptDatabase);

    const core::CorpusChunk writable{.chunk = {.text = "alpha", .lineNumber = 1, .source = "a.log"},
                                     .embedding = {1.F}};
    const auto directoryWrite = core::writeCorpusFile(directory, std::span<const core::CorpusChunk>(&writable, 1));
    REQUIRE_FALSE(directoryWrite);
    REQUIRE(directoryWrite.error() == core::EngineError::FileOpenFailure);

    std::filesystem::remove_all(directory);
}

TEST_CASE("corpus reader rejects corrupt files", "[corpus][core]")
{
    const auto directory = makeTempDir();
    const auto path = directory / "corrupt.nesso";

    const auto missing = core::readCorpusFile(directory / "missing.nesso");
    REQUIRE_FALSE(missing);
    REQUIRE(missing.error() == core::EngineError::FileOpenFailure);

    RawHeader vectorStore{};
    vectorStore.magic = {'N', 'E', 'S', 'S'};
    writeRaw(path, vectorStore);
    const auto wrongMagic = core::readCorpusFile(path);
    REQUIRE_FALSE(wrongMagic);
    REQUIRE(wrongMagic.error() == core::EngineError::CorruptDatabase);

    RawHeader future{};
    future.version = 2;
    writeRaw(path, future);
    const auto wrongVersion = core::readCorpusFile(path);
    REQUIRE_FALSE(wrongVersion);
    REQUIRE(wrongVersion.error() == core::EngineError::CorruptDatabase);

    RawHeader zeroDims{};
    zeroDims.dimensions = 0;
    zeroDims.chunkCount = 1;
    writeRaw(path, zeroDims);
    const auto zero = core::readCorpusFile(path);
    REQUIRE_FALSE(zero);
    REQUIRE(zero.error() == core::EngineError::CorruptDatabase);

    RawHeader tooMany{};
    tooMany.dimensions = 1;
    tooMany.chunkCount = 1'000'001;
    writeRaw(path, tooMany);
    const auto capped = core::readCorpusFile(path);
    REQUIRE_FALSE(capped);
    REQUIRE(capped.error() == core::EngineError::CorruptDatabase);

    RawHeader truncated{};
    truncated.dimensions = 2;
    truncated.chunkCount = 1;
    writeRaw(path, truncated, "partial");
    const auto shortFile = core::readCorpusFile(path);
    REQUIRE_FALSE(shortFile);
    REQUIRE(shortFile.error() == core::EngineError::CorruptDatabase);

    RawHeader hugeText{};
    hugeText.dimensions = 1;
    hugeText.chunkCount = 1;
    std::string record(24, '\0');
    const uint64_t textSize = 8ULL * 1024ULL * 1024ULL + 1;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    std::memcpy(record.data() + 16, &textSize, sizeof(textSize));
    writeRaw(path, hugeText, record);
    const auto huge = core::readCorpusFile(path);
    REQUIRE_FALSE(huge);
    REQUIRE(huge.error() == core::EngineError::CorruptDatabase);

    REQUIRE(core::writeCorpusFile(path, {}));
    {
        std::ofstream extra(path, std::ios::binary | std::ios::app);
        REQUIRE(extra);
        extra << 'x';
    }
    const auto trailing = core::readCorpusFile(path);
    REQUIRE_FALSE(trailing);
    REQUIRE(trailing.error() == core::EngineError::CorruptDatabase);

    std::filesystem::remove_all(directory);
}

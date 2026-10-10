// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#include <catch2/catch_all.hpp>
#include <core/corpus_index.hpp>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>
#include <vector>

namespace
{

std::filesystem::path makeTempDir()
{
    static int counter = 0;
    const auto path = std::filesystem::temp_directory_path() /
                      ("nesso_corpus_" + std::to_string(getpid()) + "_" + std::to_string(counter++));
    std::filesystem::remove_all(path);
    std::filesystem::create_directory(path);
    return path;
}

void writeBytes(const std::filesystem::path &path, std::string_view bytes)
{
    std::ofstream output(path, std::ios::binary);
    REQUIRE(output);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    REQUIRE(output);
}

void putU32(std::string &bytes, std::size_t offset, std::uint32_t value)
{
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

void putU64(std::string &bytes, std::size_t offset, std::uint64_t value)
{
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

std::string corpusHeader(std::size_t size, std::uint32_t dimensions, std::uint64_t sourceCount,
                         std::uint64_t chunkCount, std::uint64_t sourceTableOffset, std::uint64_t chunkTableOffset,
                         std::uint64_t matrixOffset)
{
    std::string bytes(size, '\0');
    bytes[0] = 'N';
    bytes[1] = 'E';
    bytes[2] = 'S';
    bytes[3] = 'C';
    putU32(bytes, 4, 2);
    putU32(bytes, 12, dimensions);
    putU64(bytes, 16, sourceCount);
    putU64(bytes, 24, chunkCount);
    putU64(bytes, 32, sourceTableOffset);
    putU64(bytes, 40, chunkTableOffset);
    putU64(bytes, 48, matrixOffset);
    return bytes;
}

std::uint64_t matrixOffset(const std::filesystem::path &path)
{
    std::ifstream input(path, std::ios::binary);
    REQUIRE(input);
    input.seekg(48);
    std::uint64_t offset = 0;
    input.read(reinterpret_cast<char *>(&offset), static_cast<std::streamsize>(sizeof(offset)));
    REQUIRE(input);
    return offset;
}

} // namespace

TEST_CASE("corpus file round-trips chunks", "[corpus][core]")
{
    const auto directory = makeTempDir();
    const auto path = directory / "corpus.nesso";

    const std::vector<core::CorpusChunk> chunks{
        {.chunk = {.text = "alpha", .lineNumber = 1, .source = "a.log", .byteOffset = 0}, .embedding = {1.F, 0.F}},
        {.chunk = {.text = "beta", .lineNumber = 4, .source = "a.log", .byteOffset = 12}, .embedding = {0.F, 1.F}},
    };
    REQUIRE(core::writeCorpusFile(path, chunks, "sentence-transformers/all-MiniLM-L6-v2"));
    REQUIRE(matrixOffset(path) % 64 == 0);

    const auto mapped = core::mapCorpusFile(path);
    REQUIRE(mapped);
    REQUIRE(mapped->modelId() == "sentence-transformers/all-MiniLM-L6-v2");
    REQUIRE(mapped->dimensions() == 2);
    REQUIRE(mapped->chunks().size() == 2);
    REQUIRE(mapped->chunks()[0].chunk.text == "alpha");
    REQUIRE(mapped->chunks()[0].chunk.byteOffset == 0);
    REQUIRE(mapped->chunks()[1].chunk.source == "a.log");
    REQUIRE(mapped->chunks()[0].embedding.empty());
    REQUIRE(mapped->matrix().size() == 4);
    REQUIRE(mapped->matrix()[0] == 1.F);
    REQUIRE(reinterpret_cast<std::uintptr_t>(mapped->matrix().data()) % 64 == 0);

    const auto read = core::readCorpusFile(path);
    REQUIRE(read);
    REQUIRE(read->size() == 2);
    REQUIRE((*read)[0].embedding.size() == 2);
    REQUIRE((*read)[0].embedding[0] == 1.F);
    REQUIRE((*read)[1].chunk.text == "beta");
    REQUIRE((*read)[1].embedding[1] == 1.F);

    std::filesystem::remove_all(directory);
}

TEST_CASE("corpus file stores source metadata", "[corpus][core]")
{
    const auto directory = makeTempDir();
    const auto source = directory / "app.log";
    writeBytes(source, "alpha\n");
    const auto path = directory / "corpus.nesso";
    const std::vector<core::CorpusChunk> chunks{
        {.chunk = {.text = "alpha", .lineNumber = 1, .source = source.string(), .byteOffset = 0}, .embedding = {1.F}},
    };

    REQUIRE(core::writeCorpusFile(path, chunks, "model"));
    const auto mapped = core::mapCorpusFile(path);
    REQUIRE(mapped);
    REQUIRE(mapped->sources().size() == 1);
    REQUIRE(mapped->sources()[0].path == source.string());
    REQUIRE(mapped->sources()[0].size == 6);
    REQUIRE(mapped->sources()[0].modifiedTime != 0);
    REQUIRE(mapped->sources()[0].tracked);

    std::filesystem::remove_all(directory);
}

TEST_CASE("empty corpus is a valid file", "[corpus][core]")
{
    const auto directory = makeTempDir();
    const auto path = directory / "empty.nesso";
    REQUIRE(core::writeCorpusFile(path, {}, "model"));
    const auto read = core::readCorpusFile(path);
    REQUIRE(read);
    REQUIRE(read->empty());
    const auto mapped = core::mapCorpusFile(path);
    REQUIRE(mapped);
    REQUIRE(mapped->matrix().empty());
    REQUIRE(mapped->modelId() == "model");
    std::filesystem::remove_all(directory);
}

TEST_CASE("corpus writer rejects bad embeddings", "[corpus][core]")
{
    const auto directory = makeTempDir();
    const auto path = directory / "bad.nesso";

    const core::CorpusChunk emptyEmbedding{.chunk = {.text = "alpha", .lineNumber = 1, .source = "a.log"},
                                           .embedding = {}};
    const auto empty = core::writeCorpusFile(path, std::span<const core::CorpusChunk>(&emptyEmbedding, 1), "model");
    REQUIRE_FALSE(empty);
    REQUIRE(empty.error() == core::EngineError::DatabaseNotInitialized);

    const std::vector<core::CorpusChunk> mismatched{
        {.chunk = {.text = "alpha", .lineNumber = 1, .source = "a.log"}, .embedding = {1.F}},
        {.chunk = {.text = "beta", .lineNumber = 2, .source = "a.log"}, .embedding = {1.F, 2.F}},
    };
    const auto mismatch = core::writeCorpusFile(path, mismatched, "model");
    REQUIRE_FALSE(mismatch);
    REQUIRE(mismatch.error() == core::EngineError::MismatchedDimensions);

    core::CorpusChunk huge{.chunk = {.text = "alpha", .lineNumber = 1, .source = "a.log"},
                           .embedding = std::vector<float>(65537, 1.F)};
    const auto oversized = core::writeCorpusFile(path, std::span<const core::CorpusChunk>(&huge, 1), "model");
    REQUIRE_FALSE(oversized);
    REQUIRE(oversized.error() == core::EngineError::CorruptDatabase);

    core::CorpusChunk longText{
        .chunk = {.text = std::string(8ULL * 1024ULL * 1024ULL + 1, 'x'), .lineNumber = 1, .source = {}},
        .embedding = {1.F}};
    const auto longWrite = core::writeCorpusFile(path, std::span<const core::CorpusChunk>(&longText, 1), "model");
    REQUIRE_FALSE(longWrite);
    REQUIRE(longWrite.error() == core::EngineError::CorruptDatabase);

    const core::CorpusChunk writable{.chunk = {.text = "alpha", .lineNumber = 1, .source = "a.log"},
                                     .embedding = {1.F}};
    const auto directoryWrite =
        core::writeCorpusFile(directory, std::span<const core::CorpusChunk>(&writable, 1), "model");
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

    std::string header(64, '\0');
    header[0] = 'N';
    header[1] = 'E';
    header[2] = 'S';
    header[3] = 'S';
    writeBytes(path, header);
    REQUIRE(core::readCorpusFile(path).error() == core::EngineError::CorruptDatabase);

    header[3] = 'C';
    header[4] = 1;
    writeBytes(path, header);
    REQUIRE(core::readCorpusFile(path).error() == core::EngineError::CorruptDatabase);

    header[4] = 2;
    const std::uint32_t dimensions = 0;
    const std::uint64_t chunkCount = 1;
    std::memcpy(header.data() + 12, &dimensions, sizeof(dimensions));
    std::memcpy(header.data() + 24, &chunkCount, sizeof(chunkCount));
    writeBytes(path, header);
    REQUIRE(core::readCorpusFile(path).error() == core::EngineError::CorruptDatabase);

    REQUIRE(core::writeCorpusFile(path, {}, "model"));
    {
        std::ofstream extra(path, std::ios::binary | std::ios::app);
        REQUIRE(extra);
        extra << 'x';
    }
    REQUIRE(core::readCorpusFile(path).error() == core::EngineError::CorruptDatabase);

    writeBytes(path, "short");
    REQUIRE(core::readCorpusFile(path).error() == core::EngineError::CorruptDatabase);

    std::filesystem::remove_all(directory);
}

TEST_CASE("checked-in corpora round-trip", "[corpus][core]")
{
    const auto one = core::readCorpusFile(NESSO_ONE_CHUNK_FIXTURE);
    REQUIRE(one);
    REQUIRE(one->size() == 1);
    REQUIRE((*one)[0].chunk.text == "checked in line");
    REQUIRE((*one)[0].chunk.lineNumber == 1);
    REQUIRE((*one)[0].chunk.source == "app.log");
    REQUIRE((*one)[0].chunk.byteOffset == 0);
    REQUIRE((*one)[0].embedding.size() == 384);
    const auto mapped = core::mapCorpusFile(NESSO_ONE_CHUNK_FIXTURE);
    REQUIRE(mapped);
    REQUIRE(mapped->modelId() == "sentence-transformers/all-MiniLM-L6-v2");
    REQUIRE(mapped->dimensions() == 384);
    REQUIRE(mapped->matrix().size() == 384);
    REQUIRE(reinterpret_cast<std::uintptr_t>(mapped->matrix().data()) % 64 == 0);

    const auto mismatch = core::readCorpusFile(NESSO_MISMATCH_FIXTURE);
    REQUIRE(mismatch);
    REQUIRE(mismatch->size() == 1);
    REQUIRE((*mismatch)[0].chunk.text == "short vector");
    REQUIRE((*mismatch)[0].embedding.size() == 2);
    REQUIRE((*mismatch)[0].embedding[1] == 1.F);
}

TEST_CASE("corpus writer rejects an unusable destination", "[corpus][core]")
{
    const auto directory = makeTempDir();
    const auto path = directory / "corpus.nesso";
    const auto longId = core::writeCorpusFile(path, {}, std::string(1025, 'm'));
    REQUIRE_FALSE(longId);
    REQUIRE(longId.error() == core::EngineError::CorruptDatabase);

    const auto missing = core::writeCorpusFile(directory / "missing" / "corpus.nesso", {}, "model");
    REQUIRE_FALSE(missing);
    REQUIRE(missing.error() == core::EngineError::FileOpenFailure);
    std::filesystem::remove_all(directory);
}

TEST_CASE("corpus reader rejects truncated tables", "[corpus][core]")
{
    const auto directory = makeTempDir();
    const auto path = directory / "corrupt.nesso";

    // The source-record length sits past EOF, so the 32-bit read fails.
    writeBytes(path, corpusHeader(132, 1, 1, 1, 124, 124, 128));
    REQUIRE(core::readCorpusFile(path).error() == core::EngineError::CorruptDatabase);

    // The chunk record starts too close to EOF for its first 64-bit field.
    writeBytes(path, corpusHeader(68, 1, 0, 1, 64, 64, 64));
    REQUIRE(core::readCorpusFile(path).error() == core::EngineError::CorruptDatabase);

    REQUIRE(core::writeCorpusFile(path, {}, "model"));
    auto mapped = core::mapCorpusFile(path);
    auto second = core::mapCorpusFile(path);
    REQUIRE(mapped);
    REQUIRE(second);
    *mapped = std::move(*second);
    REQUIRE(mapped->modelId() == "model");
    REQUIRE(mapped->matrix().empty());
    std::filesystem::remove_all(directory);
}

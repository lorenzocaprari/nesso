// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

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

    const std::vector<nesso::core::CorpusChunk> chunks{
        {.chunk = {.text = "alpha", .lineNumber = 1, .source = "a.log", .byteOffset = 0}, .embedding = {1.F, 0.F}},
        {.chunk = {.text = "beta", .lineNumber = 4, .source = "a.log", .byteOffset = 12}, .embedding = {0.F, 1.F}},
    };
    REQUIRE(nesso::core::writeCorpusFile(path, chunks, "sentence-transformers/all-MiniLM-L6-v2"));
    REQUIRE(matrixOffset(path) % 64 == 0);

    const auto mapped = nesso::core::mapCorpusFile(path);
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

    const auto read = nesso::core::readCorpusFile(path);
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
    const std::vector<nesso::core::CorpusChunk> chunks{
        {.chunk = {.text = "alpha", .lineNumber = 1, .source = source.string(), .byteOffset = 0}, .embedding = {1.F}},
    };

    REQUIRE(nesso::core::writeCorpusFile(path, chunks, "model"));
    const auto mapped = nesso::core::mapCorpusFile(path);
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
    REQUIRE(nesso::core::writeCorpusFile(path, {}, "model"));
    const auto read = nesso::core::readCorpusFile(path);
    REQUIRE(read);
    REQUIRE(read->empty());
    const auto mapped = nesso::core::mapCorpusFile(path);
    REQUIRE(mapped);
    REQUIRE(mapped->matrix().empty());
    REQUIRE(mapped->modelId() == "model");
    std::filesystem::remove_all(directory);
}

TEST_CASE("corpus writer rejects bad embeddings", "[corpus][core]")
{
    const auto directory = makeTempDir();
    const auto path = directory / "bad.nesso";

    const nesso::core::CorpusChunk emptyEmbedding{.chunk = {.text = "alpha", .lineNumber = 1, .source = "a.log"},
                                                  .embedding = {}};
    const auto empty =
        nesso::core::writeCorpusFile(path, std::span<const nesso::core::CorpusChunk>(&emptyEmbedding, 1), "model");
    REQUIRE_FALSE(empty);
    REQUIRE(empty.error() == nesso::core::EngineError::DatabaseNotInitialized);

    const std::vector<nesso::core::CorpusChunk> mismatched{
        {.chunk = {.text = "alpha", .lineNumber = 1, .source = "a.log"}, .embedding = {1.F}},
        {.chunk = {.text = "beta", .lineNumber = 2, .source = "a.log"}, .embedding = {1.F, 2.F}},
    };
    const auto mismatch = nesso::core::writeCorpusFile(path, mismatched, "model");
    REQUIRE_FALSE(mismatch);
    REQUIRE(mismatch.error() == nesso::core::EngineError::MismatchedDimensions);

    nesso::core::CorpusChunk huge{.chunk = {.text = "alpha", .lineNumber = 1, .source = "a.log"},
                                  .embedding = std::vector<float>(65537, 1.F)};
    const auto oversized =
        nesso::core::writeCorpusFile(path, std::span<const nesso::core::CorpusChunk>(&huge, 1), "model");
    REQUIRE_FALSE(oversized);
    REQUIRE(oversized.error() == nesso::core::EngineError::CorruptDatabase);

    nesso::core::CorpusChunk longText{
        .chunk = {.text = std::string(8ULL * 1024ULL * 1024ULL + 1, 'x'), .lineNumber = 1, .source = {}},
        .embedding = {1.F}};
    const auto longWrite =
        nesso::core::writeCorpusFile(path, std::span<const nesso::core::CorpusChunk>(&longText, 1), "model");
    REQUIRE_FALSE(longWrite);
    REQUIRE(longWrite.error() == nesso::core::EngineError::CorruptDatabase);

    const nesso::core::CorpusChunk writable{.chunk = {.text = "alpha", .lineNumber = 1, .source = "a.log"},
                                            .embedding = {1.F}};
    const auto directoryWrite =
        nesso::core::writeCorpusFile(directory, std::span<const nesso::core::CorpusChunk>(&writable, 1), "model");
    REQUIRE_FALSE(directoryWrite);
    REQUIRE(directoryWrite.error() == nesso::core::EngineError::FileOpenFailure);

    std::filesystem::remove_all(directory);
}

TEST_CASE("corpus reader rejects corrupt files", "[corpus][core]")
{
    const auto directory = makeTempDir();
    const auto path = directory / "corrupt.nesso";

    const auto missing = nesso::core::readCorpusFile(directory / "missing.nesso");
    REQUIRE_FALSE(missing);
    REQUIRE(missing.error() == nesso::core::EngineError::FileOpenFailure);

    std::string header(64, '\0');
    header[0] = 'N';
    header[1] = 'E';
    header[2] = 'S';
    header[3] = 'S';
    writeBytes(path, header);
    REQUIRE(nesso::core::readCorpusFile(path).error() == nesso::core::EngineError::CorruptDatabase);

    header[3] = 'C';
    header[4] = 1;
    writeBytes(path, header);
    REQUIRE(nesso::core::readCorpusFile(path).error() == nesso::core::EngineError::CorruptDatabase);

    header[4] = 2;
    const std::uint32_t dimensions = 0;
    const std::uint64_t chunkCount = 1;
    std::memcpy(header.data() + 12, &dimensions, sizeof(dimensions));
    std::memcpy(header.data() + 24, &chunkCount, sizeof(chunkCount));
    writeBytes(path, header);
    REQUIRE(nesso::core::readCorpusFile(path).error() == nesso::core::EngineError::CorruptDatabase);

    REQUIRE(nesso::core::writeCorpusFile(path, {}, "model"));
    {
        std::ofstream extra(path, std::ios::binary | std::ios::app);
        REQUIRE(extra);
        extra << 'x';
    }
    REQUIRE(nesso::core::readCorpusFile(path).error() == nesso::core::EngineError::CorruptDatabase);

    writeBytes(path, "short");
    REQUIRE(nesso::core::readCorpusFile(path).error() == nesso::core::EngineError::CorruptDatabase);

    std::filesystem::remove_all(directory);
}

TEST_CASE("checked-in corpora round-trip", "[corpus][core]")
{
    const auto one = nesso::core::readCorpusFile(NESSO_ONE_CHUNK_FIXTURE);
    REQUIRE(one);
    REQUIRE(one->size() == 1);
    REQUIRE((*one)[0].chunk.text == "checked in line");
    REQUIRE((*one)[0].chunk.lineNumber == 1);
    REQUIRE((*one)[0].chunk.source == "app.log");
    REQUIRE((*one)[0].chunk.byteOffset == 0);
    REQUIRE((*one)[0].embedding.size() == 384);
    const auto mapped = nesso::core::mapCorpusFile(NESSO_ONE_CHUNK_FIXTURE);
    REQUIRE(mapped);
    REQUIRE(mapped->modelId() == "sentence-transformers/all-MiniLM-L6-v2");
    REQUIRE(mapped->dimensions() == 384);
    REQUIRE(mapped->matrix().size() == 384);
    REQUIRE(reinterpret_cast<std::uintptr_t>(mapped->matrix().data()) % 64 == 0);

    const auto mismatch = nesso::core::readCorpusFile(NESSO_MISMATCH_FIXTURE);
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
    const auto longId = nesso::core::writeCorpusFile(path, {}, std::string(1025, 'm'));
    REQUIRE_FALSE(longId);
    REQUIRE(longId.error() == nesso::core::EngineError::CorruptDatabase);

    const auto missing = nesso::core::writeCorpusFile(directory / "missing" / "corpus.nesso", {}, "model");
    REQUIRE_FALSE(missing);
    REQUIRE(missing.error() == nesso::core::EngineError::FileOpenFailure);
    std::filesystem::remove_all(directory);
}

TEST_CASE("corpus reader rejects truncated tables", "[corpus][core]")
{
    const auto directory = makeTempDir();
    const auto path = directory / "corrupt.nesso";

    // The source-record length sits past EOF, so the 32-bit read fails.
    writeBytes(path, corpusHeader(132, 1, 1, 1, 124, 124, 128));
    REQUIRE(nesso::core::readCorpusFile(path).error() == nesso::core::EngineError::CorruptDatabase);

    // The chunk record starts too close to EOF for its first 64-bit field.
    writeBytes(path, corpusHeader(68, 1, 0, 1, 64, 64, 64));
    REQUIRE(nesso::core::readCorpusFile(path).error() == nesso::core::EngineError::CorruptDatabase);

    REQUIRE(nesso::core::writeCorpusFile(path, {}, "model"));
    auto mapped = nesso::core::mapCorpusFile(path);
    auto second = nesso::core::mapCorpusFile(path);
    REQUIRE(mapped);
    REQUIRE(second);
    *mapped = std::move(*second);
    REQUIRE(mapped->modelId() == "model");
    REQUIRE(mapped->matrix().empty());
    std::filesystem::remove_all(directory);
}

TEST_CASE("streaming corpus writer matches writeCorpusFile", "[corpus][core]")
{
    const auto directory = makeTempDir();
    const auto source = directory / "app.log";
    writeBytes(source, "alpha\n");
    nesso::core::CorpusChunk chunk;
    chunk.chunk = {.text = "alpha", .lineNumber = 1, .source = source.string(), .byteOffset = 0, .arrayElement = false};
    chunk.embedding = {1.F, 2.F, 3.F, 4.F};

    const auto directPath = directory / "direct.nesso";
    const auto streamedPath = directory / "streamed.nesso";
    REQUIRE(nesso::core::writeCorpusFile(directPath, std::span<const nesso::core::CorpusChunk>{&chunk, 1}, "model"));
    auto writer = nesso::core::CorpusWriter::create(streamedPath, "model");
    REQUIRE(writer);
    REQUIRE(writer->add(std::span<const nesso::core::CorpusChunk>{&chunk, 1}));
    REQUIRE(writer->finish());

    const auto readFile = [](const std::filesystem::path &path)
    {
        std::ifstream input(path, std::ios::binary);
        REQUIRE(input);
        return std::string{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    };
    REQUIRE(readFile(directPath) == readFile(streamedPath));

    const auto emptyDirect = directory / "empty-direct.nesso";
    const auto emptyStreamed = directory / "empty-streamed.nesso";
    REQUIRE(nesso::core::writeCorpusFile(emptyDirect, {}, "model"));
    auto emptyWriter = nesso::core::CorpusWriter::create(emptyStreamed, "model");
    REQUIRE(emptyWriter);
    REQUIRE(emptyWriter->finish());
    REQUIRE(readFile(emptyDirect) == readFile(emptyStreamed));
    std::filesystem::remove_all(directory);
}

TEST_CASE("a corpus writer rejects inconsistent chunks", "[corpus][core]")
{
    const auto directory = makeTempDir();
    const auto path = directory / "out.nesso";
    auto writer = nesso::core::CorpusWriter::create(path, "model");
    REQUIRE(writer);

    nesso::core::CorpusChunk chunk;
    chunk.chunk = {.text = "alpha", .lineNumber = 1, .source = "app.log", .byteOffset = 0, .arrayElement = false};
    const auto rejected = writer->add(std::span<const nesso::core::CorpusChunk>{&chunk, 1});
    REQUIRE_FALSE(rejected);
    REQUIRE(rejected.error() == nesso::core::EngineError::DatabaseNotInitialized);

    chunk.embedding = {1.F, 2.F, 3.F, 4.F};
    REQUIRE(writer->add(std::span<const nesso::core::CorpusChunk>{&chunk, 1}));

    nesso::core::CorpusChunk other = chunk;
    other.embedding = {1.F, 2.F};
    const auto mismatch = writer->add(std::span<const nesso::core::CorpusChunk>{&other, 1});
    REQUIRE_FALSE(mismatch);
    REQUIRE(mismatch.error() == nesso::core::EngineError::MismatchedDimensions);

    const nesso::core::CorpusChunk batch[] = {chunk, other};
    const auto mixed = writer->add(batch);
    REQUIRE_FALSE(mixed);
    REQUIRE(mixed.error() == nesso::core::EngineError::MismatchedDimensions);
    std::filesystem::remove_all(directory);
}

TEST_CASE("moving a corpus writer keeps one owner of the spill file", "[corpus][core]")
{
    const auto directory = makeTempDir();
    const auto source = directory / "app.log";
    writeBytes(source, "alpha\n");
    nesso::core::CorpusChunk chunk;
    chunk.chunk = {.text = "alpha", .lineNumber = 1, .source = source.string(), .byteOffset = 0, .arrayElement = false};
    chunk.embedding = {1.F, 2.F, 3.F, 4.F};

    const auto firstPath = directory / "first.nesso";
    const auto secondPath = directory / "second.nesso";
    auto created = nesso::core::CorpusWriter::create(firstPath, "model");
    REQUIRE(created);
    const auto abandonedRecords = firstPath.string() + ".records";
    nesso::core::CorpusWriter writer = std::move(*created);
    {
        auto other = nesso::core::CorpusWriter::create(secondPath, "model");
        REQUIRE(other);
        writer = std::move(*other);
    }
    REQUIRE_FALSE(std::filesystem::exists(abandonedRecords));
    REQUIRE(writer.add(std::span<const nesso::core::CorpusChunk>{&chunk, 1}));
    REQUIRE(writer.finish());
    REQUIRE(std::filesystem::is_regular_file(secondPath));
    REQUIRE_FALSE(std::filesystem::exists(secondPath.string() + ".records"));

    const auto droppedPath = directory / "dropped.nesso";
    {
        auto dropped = nesso::core::CorpusWriter::create(droppedPath, "model");
        REQUIRE(dropped);
        nesso::core::CorpusWriter holder = std::move(*dropped);
        REQUIRE(holder.add(std::span<const nesso::core::CorpusChunk>{&chunk, 1}));
    }
    REQUIRE_FALSE(std::filesystem::exists(droppedPath.string() + ".records"));
    std::filesystem::remove_all(directory);
}

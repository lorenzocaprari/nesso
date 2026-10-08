#include <catch2/catch_all.hpp>
#include <core/top_k.hpp>
#include <embed/onnx_embedder.hpp>
#include <embed/wordpiece_tokenizer.hpp>
#include <parser/log_chunker.hpp>

#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

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

#ifndef NESSO_MODELS_DIR
#error "NESSO_MODELS_DIR must be defined"
#endif

static std::filesystem::path modelDirOrSkip()
{
    const std::filesystem::path modelDir{NESSO_MODELS_DIR};
    if (std::filesystem::is_regular_file(modelDir / "model.onnx") &&
        std::filesystem::is_regular_file(modelDir / "vocab.txt"))
    {
        return modelDir;
    }
#ifdef NESSO_REQUIRE_MODEL
    FAIL("models/ not present; run scripts/fetch-model");
#else
    SKIP("models/ not present; run scripts/fetch-model");
#endif
    return {};
}

TEST_CASE("Semantic grep ranks the expected log line", "[grep][integration]")
{
    const std::filesystem::path modelDir = modelDirOrSkip();

    const std::filesystem::path logPath = std::filesystem::path(NESSO_TEST_FIXTURES) / "grep_sample.log";
    const auto embedder = embed::OnnxEmbedder::create(modelDir);
    REQUIRE(embedder.has_value());

    const auto chunks = parser::Chunker::fromFile(logPath);
    REQUIRE(chunks.has_value());
    REQUIRE(chunks->size() == 1);

    std::vector<std::string> texts{(*chunks)[0].text};
    const auto embeddings = (*embedder)->embedBatch(texts);
    REQUIRE(embeddings.has_value());

    const auto queryEmbedding = (*embedder)->embed("database connection error");
    REQUIRE(queryEmbedding.has_value());

    const std::span<const float> matrix{(*embeddings)[0]};
    const auto results = core::topK(*queryEmbedding, matrix, matrix.size(), 1);
    REQUIRE(results.has_value());
    REQUIRE(results->size() == 1);
    REQUIRE((*results)[0].index == 0);
    REQUIRE((*chunks)[0].text == "database connection refused");
}

TEST_CASE("WordPieceTokenizer rejects a bad vocab and splits punctuation", "[embed][integration]")
{
    const auto directory = std::filesystem::temp_directory_path() / "nesso-tokenizer-edges";
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);

    const auto missing = embed::WordPieceTokenizer::fromVocabFile(directory / "missing.txt");
    REQUIRE_FALSE(missing.has_value());
    REQUIRE(missing.error() == embed::EmbedError::VocabLoadFailure);

    const auto emptyPath = directory / "empty.txt";
    writeBytes(emptyPath, "");
    const auto empty = embed::WordPieceTokenizer::fromVocabFile(emptyPath);
    REQUIRE_FALSE(empty.has_value());
    REQUIRE(empty.error() == embed::EmbedError::VocabLoadFailure);

    const auto vocabPath = directory / "vocab.txt";
    writeBytes(vocabPath, "[PAD]\n[unused0]\n[CLS]\n[SEP]\nhello\nworld\n##ing\ntesting\n");

    const auto tokenizer = embed::WordPieceTokenizer::fromVocabFile(vocabPath, 16);
    REQUIRE(tokenizer.has_value());

    const auto punctuated = tokenizer->encode("hello, world");
    REQUIRE(punctuated.has_value());
    REQUIRE(punctuated->inputIds == std::vector<int64_t>{2, 4, 0, 5, 3});

    const auto unknown = tokenizer->encode("zzz");
    REQUIRE(unknown.has_value());
    REQUIRE(unknown->inputIds == std::vector<int64_t>{2, 0, 3});

    const auto shortTokenizer = embed::WordPieceTokenizer::fromVocabFile(vocabPath, 2);
    REQUIRE(shortTokenizer.has_value());
    const auto truncated = shortTokenizer->encode("helloing");
    REQUIRE(truncated.has_value());
    REQUIRE(truncated->inputIds == std::vector<int64_t>{2, 3});

    std::filesystem::remove_all(directory);
}

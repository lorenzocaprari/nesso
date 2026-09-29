#include <catch2/catch_all.hpp>
#include <core/embedding_store.hpp>
#include <embed/onnx_embedder.hpp>
#include <parser/log_chunker.hpp>

#include <filesystem>
#include <string>
#include <vector>

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

    const auto chunks = parser::LogChunker::fromFile(logPath);
    REQUIRE(chunks.has_value());
    REQUIRE(chunks->size() == 1);

    std::vector<std::string> texts{(*chunks)[0].text};
    const auto embeddings = (*embedder)->embedBatch(texts);
    REQUIRE(embeddings.has_value());

    core::EmbeddingStore<float> store;
    REQUIRE(store
                .insert((*embeddings)[0],
                        {.text = (*chunks)[0].text, .lineNumber = (*chunks)[0].lineNumber, .source = logPath.string()})
                .has_value());

    const auto queryEmbedding = (*embedder)->embed("database connection error");
    REQUIRE(queryEmbedding.has_value());

    const auto results = store.searchTopK(*queryEmbedding, 1);
    REQUIRE(results.has_value());
    REQUIRE(results->size() == 1);
    REQUIRE((*results)[0].chunk.text == "database connection refused");
    REQUIRE((*results)[0].chunk.source == logPath.string());
}

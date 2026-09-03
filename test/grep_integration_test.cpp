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

TEST_CASE("Semantic grep ranks the expected log line", "[grep][integration]")
{
    const std::filesystem::path modelDir = std::filesystem::path(NESSO_TEST_FIXTURES) / ".." / ".." / "models";
    if (!std::filesystem::exists(modelDir / "model.onnx"))
    {
        SKIP("models/ not present; run scripts/fetch-model");
    }

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

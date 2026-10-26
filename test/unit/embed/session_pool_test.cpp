#include <catch2/catch_all.hpp>
#include <embed/onnx_embedder.hpp>

#include <filesystem>
#include <string>
#include <vector>

#ifndef NESSO_MODELS_DIR
#error "NESSO_MODELS_DIR must be defined"
#endif

TEST_CASE("a multi-session pool matches one session byte for byte", "[session-pool][model]")
{
    const std::filesystem::path modelDir{NESSO_MODELS_DIR};
    if (!std::filesystem::is_regular_file(modelDir / "model.onnx") ||
        !std::filesystem::is_regular_file(modelDir / "vocab.txt"))
    {
#ifdef NESSO_REQUIRE_MODEL
        FAIL("models/ not present; run scripts/fetch-model");
#else
        SKIP("models/ not present; run scripts/fetch-model");
#endif
    }

    const auto single = nesso::embed::OnnxEmbedder::create(modelDir, 1);
    const auto pooled = nesso::embed::OnnxEmbedder::create(modelDir, 2);
    REQUIRE(single.has_value());
    REQUIRE(pooled.has_value());
    if (!(*pooled)->policy().quantized)
    {
        SKIP("the model is fp32; intra-op thread count is not a second session");
    }
    CHECK((*single)->policy().sessions == 1);
    CHECK((*pooled)->policy().sessions == 2);
    CHECK((*pooled)->policy().intraOpThreads == 1);

    std::vector<std::string> texts;
    texts.reserve(33);
    for (int index = 0; index < 33; ++index)
    {
        texts.push_back("database connection refused " + std::to_string(index));
    }

    const auto fromOne = (*single)->embedBatch(texts);
    const auto fromPool = (*pooled)->embedBatch(texts);
    const auto again = (*pooled)->embedBatch(texts);
    REQUIRE(fromOne.has_value());
    REQUIRE(fromPool.has_value());
    REQUIRE(again.has_value());
    REQUIRE(fromOne->size() == texts.size());
    REQUIRE(*fromOne == *fromPool);
    REQUIRE(*fromPool == *again);
    CHECK(fromPool->front() != fromPool->back());
}

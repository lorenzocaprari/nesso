#include <catch2/catch_all.hpp>
#include <mach_embed/onnx_embedder.hpp>

#include <cmath>
#include <filesystem>

#ifndef MACH1_TEST_FIXTURES
#error "MACH1_TEST_FIXTURES must be defined"
#endif

TEST_CASE("OnnxEmbedder produces unit-normal 384-d embeddings", "[OnnxEmbedder][embed][integration]")
{
    const std::filesystem::path modelDir = std::filesystem::path(MACH1_TEST_FIXTURES) / ".." / ".." / "models";
    if (!std::filesystem::exists(modelDir / "model.onnx"))
    {
        SKIP("models/ not present; run scripts/fetch-model");
    }

    const auto embedder = mach_embed::OnnxEmbedder::create(modelDir);
    REQUIRE(embedder.has_value());

    const auto embedding = (*embedder)->embed("database connection refused");
    REQUIRE(embedding.has_value());
    REQUIRE(embedding->size() == mach_embed::MINILM_EMBEDDING_DIMENSIONS);

    float norm = 0.0F;
    for (const float value : *embedding)
    {
        norm += value * value;
    }
    REQUIRE(norm == Catch::Approx(1.0F).margin(1e-3F));
}

TEST_CASE("OnnxEmbedder ranks similar strings above dissimilar ones", "[OnnxEmbedder][embed][integration]")
{
    const std::filesystem::path modelDir = std::filesystem::path(MACH1_TEST_FIXTURES) / ".." / ".." / "models";
    if (!std::filesystem::exists(modelDir / "model.onnx"))
    {
        SKIP("models/ not present; run scripts/fetch-model");
    }

    const auto embedder = mach_embed::OnnxEmbedder::create(modelDir);
    REQUIRE(embedder.has_value());

    const auto query = (*embedder)->embed("database connection refused");
    const auto similar = (*embedder)->embed("db connection failed");
    const auto dissimilar = (*embedder)->embed("sunny beach vacation");
    REQUIRE(query.has_value());
    REQUIRE(similar.has_value());
    REQUIRE(dissimilar.has_value());

    const auto dot = [](const std::vector<float> &left, const std::vector<float> &right)
    {
        float score = 0.0F;
        for (size_t i = 0; i < left.size(); ++i)
        {
            score += left[i] * right[i];
        }
        return score;
    };

    REQUIRE(dot(*query, *similar) > dot(*query, *dissimilar));
}

TEST_CASE("OnnxEmbedder embedBatch matches per-string embed", "[OnnxEmbedder][embed][integration]")
{
    const std::filesystem::path modelDir = std::filesystem::path(MACH1_TEST_FIXTURES) / ".." / ".." / "models";
    if (!std::filesystem::exists(modelDir / "model.onnx"))
    {
        SKIP("models/ not present; run scripts/fetch-model");
    }

    const auto embedder = mach_embed::OnnxEmbedder::create(modelDir);
    REQUIRE(embedder.has_value());

    const std::array<std::string, 2> texts{"payment timeout", "database connection refused"};
    const auto batch = (*embedder)->embedBatch(texts);
    REQUIRE(batch.has_value());
    REQUIRE(batch->size() == texts.size());

    for (size_t i = 0; i < texts.size(); ++i)
    {
        const auto single = (*embedder)->embed(texts[i]);
        REQUIRE(single.has_value());
        REQUIRE((*batch)[i].size() == single->size());
        float cosine = 0.0F;
        for (size_t dim = 0; dim < single->size(); ++dim)
        {
            cosine += (*batch)[i][dim] * (*single)[dim];
        }
        REQUIRE(cosine > 0.99F);
    }
}

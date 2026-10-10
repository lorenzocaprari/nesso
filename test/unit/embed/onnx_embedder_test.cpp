#include <catch2/catch_all.hpp>
#include <embed/onnx_embedder.hpp>

#include <cmath>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

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

TEST_CASE("OnnxEmbedder produces unit-normal 384-d embeddings", "[OnnxEmbedder][embed][integration]")
{
    const std::filesystem::path modelDir = modelDirOrSkip();

    const auto embedder = embed::OnnxEmbedder::create(modelDir);
    REQUIRE(embedder.has_value());

    const auto embedding = (*embedder)->embed("database connection refused");
    REQUIRE(embedding.has_value());
    REQUIRE(embedding->size() == embed::MINILM_EMBEDDING_DIMENSIONS);

    float norm = 0.0F;
    for (const float value : *embedding)
    {
        norm += value * value;
    }
    REQUIRE(norm == Catch::Approx(1.0F).margin(1e-3F));
}

TEST_CASE("OnnxEmbedder ranks similar strings above dissimilar ones", "[OnnxEmbedder][embed][integration]")
{
    const std::filesystem::path modelDir = modelDirOrSkip();

    const auto embedder = embed::OnnxEmbedder::create(modelDir);
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
    const std::filesystem::path modelDir = modelDirOrSkip();

    const auto embedder = embed::OnnxEmbedder::create(modelDir);
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
        // Dynamic quantization scales across the padded batch, so a batched
        // embed and a single embed of the same text differ by about 0.0125.
        REQUIRE(cosine > 0.98F);
    }
}

static float cosineSimilarity(const std::vector<float> &left, const std::vector<float> &right)
{
    float score = 0.0F;
    for (size_t dim = 0; dim < left.size(); ++dim)
    {
        score += left[dim] * right[dim];
    }
    return score;
}

TEST_CASE("OnnxEmbedder mixed-length batch matches a single embed", "[OnnxEmbedder][embed][integration]")
{
    const std::filesystem::path modelDir = modelDirOrSkip();

    const auto embedder = embed::OnnxEmbedder::create(modelDir);
    REQUIRE(embedder.has_value());

    const std::string shortText = "database connection refused";
    std::string longText;
    for (int repeat = 0; repeat < 30; ++repeat)
    {
        longText += "upstream database connection timeout while waiting for the replica ";
    }

    std::vector<std::string> texts;
    texts.push_back(shortText);
    texts.push_back(longText);
    for (int index = 0; index < 31; ++index)
    {
        texts.push_back("cache miss " + std::to_string(index));
    }

    const auto batch = (*embedder)->embedBatch(texts);
    REQUIRE(batch.has_value());
    REQUIRE(batch->size() == texts.size());

    const auto shortAlone = (*embedder)->embed(shortText);
    const auto longAlone = (*embedder)->embed(longText);
    REQUIRE(shortAlone.has_value());
    REQUIRE(longAlone.has_value());
    REQUIRE(cosineSimilarity((*batch)[0], *shortAlone) > 0.98F);
    REQUIRE(cosineSimilarity((*batch)[1], *longAlone) > 0.98F);
}

TEST_CASE("OnnxEmbedder reports completed batches", "[OnnxEmbedder][embed][integration]")
{
    const std::filesystem::path modelDir = modelDirOrSkip();

    const auto embedder = embed::OnnxEmbedder::create(modelDir);
    REQUIRE(embedder.has_value());

    std::vector<std::string> texts;
    for (int index = 0; index < 33; ++index)
    {
        texts.push_back("progress sample " + std::to_string(index));
    }

    std::vector<std::pair<size_t, size_t>> progress;
    const auto batch = (*embedder)->embedBatch(texts, [&progress](size_t completed, size_t total)
                                               { progress.emplace_back(completed, total); });
    REQUIRE(batch.has_value());
    REQUIRE(progress == std::vector<std::pair<size_t, size_t>>{{32, 33}, {33, 33}});
}

TEST_CASE("OnnxEmbedder repeats an embedding", "[OnnxEmbedder][embed][integration]")
{
    const std::filesystem::path modelDir = modelDirOrSkip();

    const auto embedder = embed::OnnxEmbedder::create(modelDir);
    REQUIRE(embedder.has_value());

    const auto first = (*embedder)->embed("database connection refused");
    REQUIRE(first.has_value());
    for (int run = 0; run < 20; ++run)
    {
        const auto again = (*embedder)->embed("database connection refused");
        REQUIRE(again.has_value());
        REQUIRE(cosineSimilarity(*first, *again) > 0.9999F);
    }
}

#include <catch2/catch_all.hpp>
#include <embed/onnx_embedder.hpp>

#include <cerrno>
#include <cmath>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#if defined(__linux__) && defined(__x86_64__)
#include <sys/syscall.h>
#include <unistd.h>
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

TEST_CASE("OnnxEmbedder produces unit-normal 384-d embeddings", "[OnnxEmbedder][embed][integration]")
{
    const std::filesystem::path modelDir = modelDirOrSkip();

    const auto embedder = nesso::embed::OnnxEmbedder::create(modelDir);
    REQUIRE(embedder.has_value());

    const auto embedding = (*embedder)->embed("database connection refused");
    REQUIRE(embedding.has_value());
    REQUIRE(embedding->size() == nesso::embed::MINILM_EMBEDDING_DIMENSIONS);

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

    const auto embedder = nesso::embed::OnnxEmbedder::create(modelDir);
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

    const auto embedder = nesso::embed::OnnxEmbedder::create(modelDir);
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

    const auto embedder = nesso::embed::OnnxEmbedder::create(modelDir);
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

    const auto embedder = nesso::embed::OnnxEmbedder::create(modelDir);
    REQUIRE(embedder.has_value());

    std::vector<std::string> texts;
    for (int index = 0; index < 65; ++index)
    {
        texts.push_back("progress sample " + std::to_string(index));
    }

    std::vector<std::pair<size_t, size_t>> progress;
    const auto batch = (*embedder)->embedBatch(texts, [&progress](size_t completed, size_t total)
                                               { progress.emplace_back(completed, total); });
    REQUIRE(batch.has_value());
    REQUIRE(progress == std::vector<std::pair<size_t, size_t>>{{32, 65}, {64, 65}, {65, 65}});
}

TEST_CASE("OnnxEmbedder repeats an embedding", "[OnnxEmbedder][embed][integration]")
{
    const std::filesystem::path modelDir = modelDirOrSkip();

    const auto embedder = nesso::embed::OnnxEmbedder::create(modelDir);
    REQUIRE(embedder.has_value());

    const auto first = (*embedder)->embed("database connection refused");
    REQUIRE(first.has_value());
    for (int run = 0; run < 20; ++run)
    {
        const auto again = (*embedder)->embed("database connection refused");
        REQUIRE(again.has_value());
        REQUIRE(*again == *first);
    }
}

TEST_CASE("OnnxEmbedder int8 pool repeats a two-batch embed byte for byte", "[OnnxEmbedder][embed][integration]")
{
    const std::filesystem::path modelDir = modelDirOrSkip();

    const auto pooled = nesso::embed::OnnxEmbedder::create(modelDir, 2);
    REQUIRE(pooled.has_value());
    if (!(*pooled)->policy().quantized)
    {
        SKIP("the model is fp32");
    }

#if defined(__linux__) && defined(__x86_64__)
    // ORT asks for AMX tile data when it builds its environment. On a host
    // that loses tiles across a context switch, an AMX GEMM is corrupted when
    // it is preempted, so the request must have been refused.
    constexpr long kArchReqXcompPerm = 0x1023;
    constexpr long kXfeatureXtiledata = 18;
    errno = 0;
    const long request = syscall(SYS_arch_prctl, kArchReqXcompPerm, kXfeatureXtiledata);
    const int requestErrno = errno;
    CHECK(request == -1);
    CHECK(requestErrno == EPERM);
#endif

    // These short lines fill two runs of 32. Each repeat must match the first byte for byte.
    std::vector<std::string> texts;
    texts.reserve(64);
    for (int index = 0; index < 64; ++index)
    {
        texts.push_back("upstream timeout on replica " + std::to_string(index) + " after retry");
    }
    const auto first = (*pooled)->embedBatch(texts);
    REQUIRE(first.has_value());
    REQUIRE(first->size() == texts.size());
    for (int run = 0; run < 10; ++run)
    {
        const auto again = (*pooled)->embedBatch(texts);
        REQUIRE(again.has_value());
        REQUIRE(*again == *first);
    }
}

TEST_CASE("int8 bytes change when texts share a padded batch", "[OnnxEmbedder][embed][integration]")
{
    const std::filesystem::path modelDir = modelDirOrSkip();

    const auto embedder = nesso::embed::OnnxEmbedder::create(modelDir, 1);
    REQUIRE(embedder.has_value());
    REQUIRE((*embedder)->policy().quantized);

    const std::string anchor = "database connection refused";
    std::string partner;
    for (int repeat = 0; repeat < 40; ++repeat)
    {
        partner += "upstream database connection timeout while waiting for the replica ";
    }
    const std::vector<std::string> together{anchor, partner};
    const auto batched = (*embedder)->embedBatch(together);
    const auto alone = (*embedder)->embed(anchor);
    REQUIRE(batched.has_value());
    REQUIRE(alone.has_value());
    REQUIRE(batched->size() == together.size());
    REQUIRE(batched->front() != *alone);
}

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

static std::filesystem::path ModelDirOrSkip()
{
    const std::filesystem::path model_dir{NESSO_MODELS_DIR};
    if (std::filesystem::is_regular_file(model_dir / "model.onnx") &&
        std::filesystem::is_regular_file(model_dir / "vocab.txt"))
    {
        return model_dir;
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
    const std::filesystem::path model_dir = ModelDirOrSkip();

    const auto embedder = nesso::embed::OnnxEmbedder::Create(model_dir);
    REQUIRE(embedder.has_value());

    const auto embedding = (*embedder)->Embed("database connection refused");
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
    const std::filesystem::path model_dir = ModelDirOrSkip();

    const auto embedder = nesso::embed::OnnxEmbedder::Create(model_dir);
    REQUIRE(embedder.has_value());

    const auto query = (*embedder)->Embed("database connection refused");
    const auto similar = (*embedder)->Embed("db connection failed");
    const auto dissimilar = (*embedder)->Embed("sunny beach vacation");
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

TEST_CASE("OnnxEmbedder EmbedBatch matches per-string embed", "[OnnxEmbedder][embed][integration]")
{
    const std::filesystem::path model_dir = ModelDirOrSkip();

    const auto embedder = nesso::embed::OnnxEmbedder::Create(model_dir);
    REQUIRE(embedder.has_value());

    const std::array<std::string, 2> texts{"payment timeout", "database connection refused"};
    const auto batch = (*embedder)->EmbedBatch(texts);
    REQUIRE(batch.has_value());
    REQUIRE(batch->size() == texts.size());

    for (size_t i = 0; i < texts.size(); ++i)
    {
        const auto single = (*embedder)->Embed(texts[i]);
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

static float CosineSimilarity(const std::vector<float> &left, const std::vector<float> &right)
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
    const std::filesystem::path model_dir = ModelDirOrSkip();

    const auto embedder = nesso::embed::OnnxEmbedder::Create(model_dir);
    REQUIRE(embedder.has_value());

    const std::string short_text = "database connection refused";
    std::string long_text;
    for (int repeat = 0; repeat < 30; ++repeat)
    {
        long_text += "upstream database connection timeout while waiting for the replica ";
    }

    std::vector<std::string> texts;
    texts.push_back(short_text);
    texts.push_back(long_text);
    for (int index = 0; index < 31; ++index)
    {
        texts.push_back("cache miss " + std::to_string(index));
    }

    const auto batch = (*embedder)->EmbedBatch(texts);
    REQUIRE(batch.has_value());
    REQUIRE(batch->size() == texts.size());

    const auto short_alone = (*embedder)->Embed(short_text);
    const auto long_alone = (*embedder)->Embed(long_text);
    REQUIRE(short_alone.has_value());
    REQUIRE(long_alone.has_value());
    REQUIRE(CosineSimilarity((*batch)[0], *short_alone) > 0.98F);
    REQUIRE(CosineSimilarity((*batch)[1], *long_alone) > 0.98F);
}

TEST_CASE("OnnxEmbedder reports completed batches", "[OnnxEmbedder][embed][integration]")
{
    const std::filesystem::path model_dir = ModelDirOrSkip();

    const auto embedder = nesso::embed::OnnxEmbedder::Create(model_dir);
    REQUIRE(embedder.has_value());

    std::vector<std::string> texts;
    for (int index = 0; index < 65; ++index)
    {
        texts.push_back("progress sample " + std::to_string(index));
    }

    std::vector<std::pair<size_t, size_t>> progress;
    const auto batch = (*embedder)->EmbedBatch(texts, [&progress](size_t completed, size_t total)
                                               { progress.emplace_back(completed, total); });
    REQUIRE(batch.has_value());
    REQUIRE(progress == std::vector<std::pair<size_t, size_t>>{{32, 65}, {64, 65}, {65, 65}});
}

TEST_CASE("OnnxEmbedder repeats an embedding", "[OnnxEmbedder][embed][integration]")
{
    const std::filesystem::path model_dir = ModelDirOrSkip();

    const auto embedder = nesso::embed::OnnxEmbedder::Create(model_dir);
    REQUIRE(embedder.has_value());

    const auto first = (*embedder)->Embed("database connection refused");
    REQUIRE(first.has_value());
    for (int run = 0; run < 20; ++run)
    {
        const auto again = (*embedder)->Embed("database connection refused");
        REQUIRE(again.has_value());
        REQUIRE(*again == *first);
    }
}

TEST_CASE("OnnxEmbedder int8 pool repeats a two-batch embed byte for byte", "[OnnxEmbedder][embed][integration]")
{
    const std::filesystem::path model_dir = ModelDirOrSkip();

    const auto pooled = nesso::embed::OnnxEmbedder::Create(model_dir, 2);
    REQUIRE(pooled.has_value());
    if (!(*pooled)->Policy().quantized)
    {
        SKIP("the model is fp32");
    }

#if defined(__linux__) && defined(__x86_64__)
    // ORT asks for AMX tile data when it builds its environment. On a host
    // that loses tiles across a context switch, an AMX GEMM is corrupted when
    // it is preempted, so the request must have been refused.
    constexpr long k_arch_req_xcomp_perm = 0x1023;
    constexpr long k_xfeature_xtiledata = 18;
    errno = 0;
    const long request = syscall(SYS_arch_prctl, k_arch_req_xcomp_perm, k_xfeature_xtiledata);
    const int request_errno = errno;
    CHECK(request == -1);
    CHECK(request_errno == EPERM);
#endif

    // These short lines fill two runs of 32. Each repeat must match the first
    // byte for byte.
    std::vector<std::string> texts;
    texts.reserve(64);
    for (int index = 0; index < 64; ++index)
    {
        texts.push_back("upstream timeout on replica " + std::to_string(index) + " after retry");
    }
    const auto first = (*pooled)->EmbedBatch(texts);
    REQUIRE(first.has_value());
    REQUIRE(first->size() == texts.size());
    for (int run = 0; run < 10; ++run)
    {
        const auto again = (*pooled)->EmbedBatch(texts);
        REQUIRE(again.has_value());
        REQUIRE(*again == *first);
    }
}

TEST_CASE("int8 bytes change when texts share a padded batch", "[OnnxEmbedder][embed][integration]")
{
    const std::filesystem::path model_dir = ModelDirOrSkip();

    const auto embedder = nesso::embed::OnnxEmbedder::Create(model_dir, 1);
    REQUIRE(embedder.has_value());
    REQUIRE((*embedder)->Policy().quantized);

    const std::string anchor = "database connection refused";
    std::string partner;
    for (int repeat = 0; repeat < 40; ++repeat)
    {
        partner += "upstream database connection timeout while waiting for the replica ";
    }
    const std::vector<std::string> together{anchor, partner};
    const auto batched = (*embedder)->EmbedBatch(together);
    const auto alone = (*embedder)->Embed(anchor);
    REQUIRE(batched.has_value());
    REQUIRE(alone.has_value());
    REQUIRE(batched->size() == together.size());
    REQUIRE(batched->front() != *alone);
}

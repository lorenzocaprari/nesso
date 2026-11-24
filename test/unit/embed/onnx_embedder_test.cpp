#include <catch2/catch_all.hpp>
#include <embed/onnx_embedder.hpp>

#include <cerrno>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <initializer_list>
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

    // GCC 16 -O3 -fsanitize=thread reports a false maybe-uninitialized in
    // libstdc++ basic_string when this array is aggregate-initialized.
    std::vector<std::string> texts;
    texts.emplace_back("payment timeout");
    texts.emplace_back("database connection refused");
    const auto batch = (*embedder)->EmbedBatch(texts);
    REQUIRE(batch.has_value());
    REQUIRE(batch->size() == texts.size());

    for (size_t i = 0; i < texts.size(); ++i)
    {
        const auto single = (*embedder)->Embed(texts[i]);
        REQUIRE(single.has_value());
        // Each text runs alone, so a batched embed is the single embed.
        REQUIRE((*batch)[i] == *single);
    }
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
    REQUIRE((*batch)[0] == *short_alone);
    REQUIRE((*batch)[1] == *long_alone);
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

TEST_CASE("a text's vector does not depend on the texts embedded with it", "[OnnxEmbedder][embed][integration]")
{
    const std::filesystem::path model_dir = ModelDirOrSkip();

    // Neighbours of many lengths, so a padded run would pad the anchor and
    // share the int8 activation scale with them (#362).
    std::vector<std::string> neighbours;
    neighbours.reserve(40);
    for (int index = 0; index < 40; ++index)
    {
        std::string neighbour = "node " + std::to_string(index) + " reported";
        for (int word = 0; word < index; ++word)
        {
            neighbour += " upstream replica timeout";
        }
        neighbours.push_back(std::move(neighbour));
    }
    const std::string anchor = "database connection refused";

    for (const size_t threads : {size_t{1}, size_t{2}})
    {
        CAPTURE(threads);
        const auto embedder = nesso::embed::OnnxEmbedder::Create(model_dir, threads);
        REQUIRE(embedder.has_value());
        const auto alone = (*embedder)->Embed(anchor);
        REQUIRE(alone.has_value());

        for (const size_t position : {size_t{0}, neighbours.size() / 2, neighbours.size()})
        {
            CAPTURE(position);
            std::vector<std::string> texts = neighbours;
            texts.insert(texts.begin() + static_cast<std::ptrdiff_t>(position), anchor);
            const auto together = (*embedder)->EmbedBatch(texts);
            REQUIRE(together.has_value());
            REQUIRE(together->size() == texts.size());
            REQUIRE((*together)[position] == *alone);
        }
    }
}

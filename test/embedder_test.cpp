#include <catch2/catch_all.hpp>
#include <cmath>
#include <mach_core/embedder.hpp>

using namespace mach_core;

TEST_CASE("HashEmbedder returns a 384-d unit vector", "[Embedder][core][Unit]")
{
    HashEmbedder embedder;
    auto a = embedder.embed("CAN timeout on bus 0x1A4");
    REQUIRE(a.has_value());
    REQUIRE(a->size() == kEmbeddingDims);

    float sumSq = 0.0f;
    for (float v : *a)
    {
        sumSq += v * v;
    }
    REQUIRE(sumSq == Catch::Approx(1.0f).margin(1e-5f));
}

TEST_CASE("HashEmbedder is deterministic", "[Embedder][core][Unit]")
{
    HashEmbedder embedder;
    auto a = embedder.embed("heartbeat ok");
    auto b = embedder.embed("heartbeat ok");
    auto c = embedder.embed("different line");
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    REQUIRE(c.has_value());
    REQUIRE(*a == *b);
    REQUIRE_FALSE(*a == *c);
}

TEST_CASE("makeEmbedder without a model returns HashEmbedder", "[Embedder][core][Unit]")
{
    auto embedder = makeEmbedder({}, {});
    REQUIRE(embedder.has_value());
    auto values = (*embedder)->embed("hello");
    REQUIRE(values.has_value());
    REQUIRE(values->size() == kEmbeddingDims);
}

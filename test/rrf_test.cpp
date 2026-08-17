#include <catch2/catch_all.hpp>
#include <mach_core/rrf.hpp>
#include <mach_core/vector_search.hpp>

using namespace mach_core;

TEST_CASE("fuseRrf boosts documents that appear in both lists", "[Rrf][core][Unit]")
{
    const std::vector<SearchResult<float>> semantic{{.index = 1, .score = 0.9f}, {.index = 2, .score = 0.8f},
                                                    {.index = 3, .score = 0.1f}};
    const std::vector<SearchResult<float>> lexical{{.index = 3, .score = 5.0f}, {.index = 1, .score = 1.0f}};

    auto fused = fuseRrf(semantic, lexical, 3, 60);
    REQUIRE(fused.size() == 3);
    REQUIRE(fused.front().index == 1);
}

TEST_CASE("fuseRrf returns empty for k=0", "[Rrf][core][Unit]")
{
    const std::vector<SearchResult<float>> semantic{{.index = 1, .score = 1.0f}};
    REQUIRE(fuseRrf(semantic, {}, 0).empty());
}

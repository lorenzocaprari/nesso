#include <catch2/catch_all.hpp>
#include <string>
#include <mach_core/bm25_index.hpp>

using namespace mach_core;

TEST_CASE("lexicalTokens keeps hex CAN ids intact", "[Bm25][core][Unit]")
{
    const auto tokens = lexicalTokens("CAN timeout on bus 0x1A4");
    REQUIRE_THAT(tokens, Catch::Matchers::VectorContains(std::string{"0x1a4"}));
    REQUIRE_THAT(tokens, Catch::Matchers::VectorContains(std::string{"can"}));
}

TEST_CASE("Bm25Index ranks an exact hex token first", "[Bm25][core][Unit]")
{
    Bm25Index index;
    index.addDocument(0, "boot sequence complete");
    index.addDocument(1, "CAN timeout on bus 0x1A4");
    index.addDocument(2, "heartbeat ok");

    auto hits = index.search("0x1A4", 3);
    REQUIRE_FALSE(hits.empty());
    REQUIRE(hits.front().index == 1);
}

TEST_CASE("Bm25Index returns nothing for an empty index", "[Bm25][core][Unit]")
{
    Bm25Index index;
    REQUIRE(index.search("timeout", 5).empty());
}

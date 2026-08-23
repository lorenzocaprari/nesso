#include <array>
#include <catch2/catch_all.hpp>
#include <core/core_types.hpp>
#include <core/embedding_store.hpp>
#include <core/vector_search.hpp>
#include <span>
#include <vector>

using namespace core;

TEST_CASE("EmbeddingStore ranks normalized embeddings by dot product", "[EmbeddingStore][core][Unit]")
{
    EmbeddingStore<float> store;

    REQUIRE(store.insert(std::array<float, 3>{1.0F, 0.0F, 0.0F}, {.text = "alpha", .lineNumber = 1}).has_value());
    REQUIRE(store.insert(std::array<float, 3>{0.0F, 1.0F, 0.0F}, {.text = "beta", .lineNumber = 2}).has_value());
    REQUIRE(store.insert(std::array<float, 3>{0.7F, 0.7F, 0.0F}, {.text = "gamma", .lineNumber = 3}).has_value());
    REQUIRE(store.size() == 3);

    const auto results = store.searchTopK(std::array<float, 3>{0.6F, 0.8F, 0.0F}, 2);
    REQUIRE(results.has_value());
    REQUIRE(results->size() == 2);
    REQUIRE((*results)[0].chunk.text == "gamma");
    REQUIRE((*results)[1].chunk.text == "beta");
}

TEST_CASE("EmbeddingStore preserves metadata and tie order", "[EmbeddingStore][core][Unit]")
{
    EmbeddingStore<float> store;
    REQUIRE(store.insert(std::array<float, 3>{1.0F, 0.0F, 0.0F}, {.text = "first", .lineNumber = 10}).has_value());
    REQUIRE(store.insert(std::array<float, 3>{1.0F, 0.0F, 0.0F}, {.text = "second", .lineNumber = 20}).has_value());

    const auto results = store.searchTopK(std::array<float, 3>{1.0F, 0.0F, 0.0F}, 2);
    REQUIRE(results.has_value());
    REQUIRE((*results)[0].index == 0);
    REQUIRE((*results)[0].chunk.lineNumber == 10);
    REQUIRE((*results)[1].index == 1);
}

TEST_CASE("EmbeddingStore rejects dimension mismatch", "[EmbeddingStore][core][Unit]")
{
    EmbeddingStore<float> store;
    REQUIRE(store.insert(std::array<float, 3>{1.0F, 0.0F, 0.0F}, {.text = "ok", .lineNumber = 1}).has_value());

    const auto badInsert = store.insert(std::array<float, 2>{1.0F, 0.0F}, {.text = "bad", .lineNumber = 2});
    REQUIRE_FALSE(badInsert.has_value());
    REQUIRE(badInsert.error() == EngineError::MismatchedDimensions);

    const auto badSearch = store.searchTopK(std::array<float, 2>{1.0F, 0.0F}, 1);
    REQUIRE_FALSE(badSearch.has_value());
    REQUIRE(badSearch.error() == EngineError::MismatchedDimensions);
}

TEST_CASE("EmbeddingStore clamps k to collection size", "[EmbeddingStore][core][Unit]")
{
    EmbeddingStore<float> store;
    REQUIRE(store.insert(std::array<float, 2>{1.0F, 0.0F}, {.text = "only", .lineNumber = 1}).has_value());

    const auto results = store.searchTopK(std::array<float, 2>{1.0F, 0.0F}, 10);
    REQUIRE(results.has_value());
    REQUIRE(results->size() == 1);
}

TEST_CASE("EmbeddingStore handles empty collection and invalid inserts", "[EmbeddingStore][core][Unit]")
{
    EmbeddingStore<float> store;
    REQUIRE(store.size() == 0);

    const auto emptyInsert = store.insert(std::span<const float>{}, {.text = "empty", .lineNumber = 1});
    REQUIRE_FALSE(emptyInsert.has_value());
    REQUIRE(emptyInsert.error() == EngineError::DatabaseNotInitialized);
    REQUIRE(store.size() == 0);

    const auto emptySearch = store.searchTopK(std::array<float, 3>{1.0F, 0.0F, 0.0F}, 1);
    REQUIRE(emptySearch.has_value());
    REQUIRE(emptySearch->empty());

    REQUIRE(store.insert(std::array<float, 3>{1.0F, 0.0F, 0.0F}, {.text = "ok", .lineNumber = 1}).has_value());
    REQUIRE(store.size() == 1);

    const auto zeroK = store.searchTopK(std::array<float, 3>{1.0F, 0.0F, 0.0F}, 0);
    REQUIRE(zeroK.has_value());
    REQUIRE(zeroK->empty());
}

TEST_CASE("EmbeddingStore<double> insert and search", "[EmbeddingStore][core][Unit]")
{
    EmbeddingStore<double> store;
    REQUIRE_FALSE(store.insert(std::span<const double>{}, {.text = "empty", .lineNumber = 1}).has_value());
    REQUIRE(store.insert(std::array<double, 2>{1.0, 0.0}, {.text = "hit", .lineNumber = 1}).has_value());
    REQUIRE(store.size() == 1);
    REQUIRE_FALSE(store.insert(std::array<double, 1>{1.0}, {.text = "bad", .lineNumber = 2}).has_value());

    const auto results = store.searchTopK(std::array<double, 2>{1.0, 0.0}, 1);
    REQUIRE(results.has_value());
    REQUIRE(results->size() == 1);
    REQUIRE((*results)[0].chunk.text == "hit");
    REQUIRE((*results)[0].score == Catch::Approx(1.0));

    REQUIRE_FALSE(store.searchTopK(std::array<double, 1>{1.0}, 1).has_value());
    REQUIRE(store.searchTopK(std::array<double, 2>{1.0, 0.0}, 0)->empty());
}

TEST_CASE("selectTopKInPlace clears on empty or zero k", "[EmbeddingStore][core][Unit]")
{
    const auto byScore = [](const EmbeddingSearchResult<float> &left, const EmbeddingSearchResult<float> &right)
    { return left.score > right.score; };

    std::vector<EmbeddingSearchResult<float>> results{
        {.index = 0, .score = 1.0F, .chunk = {.text = "a", .lineNumber = 1}}};
    detail::selectTopKInPlace(results, 0, byScore);
    REQUIRE(results.empty());

    std::vector<EmbeddingSearchResult<float>> empty;
    detail::selectTopKInPlace(empty, 1, byScore);
    REQUIRE(empty.empty());
}

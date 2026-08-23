// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#include <algorithm>
#include <catch2/catch_all.hpp>
#include <cmath>
#include <core/core_types.hpp>
#include <core/distance.hpp>
#include <core/storage_engine.hpp>
#include <core/vector_search.hpp>
#include <cstdlib>
#include <filesystem>
#include <random>
#include <vector>

namespace fs = std::filesystem;
using namespace core;
using core::math::CosineSimilarity;

namespace
{

class TestDatabaseGuard
{
  public:
    explicit TestDatabaseGuard(fs::path path) : m_path(std::move(path)) {}
    ~TestDatabaseGuard() { fs::remove(m_path); }

  private:
    fs::path m_path;
};

[[nodiscard]] fs::path makeTempDbPath()
{
    return fs::temp_directory_path() / ("search_oracle_" + std::to_string(std::rand()));
}

[[nodiscard]] float naiveCosine(std::span<const float> left, std::span<const float> right)
{
    REQUIRE(left.size() == right.size());
    REQUIRE_FALSE(left.empty());

    double dot = 0.0;
    double normLeft = 0.0;
    double normRight = 0.0;
    for (size_t index = 0; index < left.size(); ++index)
    {
        const double a = static_cast<double>(left[index]);
        const double b = static_cast<double>(right[index]);
        dot += a * b;
        normLeft += a * a;
        normRight += b * b;
    }
    if (normLeft == 0.0 || normRight == 0.0)
    {
        return 0.0f;
    }
    return static_cast<float>(dot / (std::sqrt(normLeft) * std::sqrt(normRight)));
}

[[nodiscard]] std::vector<SearchResult<float>> naiveTopK(const std::vector<std::vector<float>> &vectors,
                                                         std::span<const float> query, size_t k)
{
    std::vector<SearchResult<float>> scored;
    scored.reserve(vectors.size());
    for (uint64_t index = 0; index < vectors.size(); ++index)
    {
        scored.push_back({.index = index, .score = naiveCosine(query, vectors[index])});
    }

    const auto order = [](const SearchResult<float> &left, const SearchResult<float> &right)
    { return left.score != right.score ? left.score > right.score : left.index < right.index; };
    std::sort(scored.begin(), scored.end(), order);
    if (k < scored.size())
    {
        scored.resize(k);
    }
    return scored;
}

} // namespace

TEST_CASE("CosineSimilarity matches double-precision oracle on random inputs", "[CosineSimilarity][oracle][Unit]")
{
    auto seed = GENERATE(take(32, random(1, 1000000)));
    std::mt19937 rng(static_cast<std::mt19937::result_type>(seed));
    std::uniform_int_distribution<size_t> dimDist(1, 24);
    std::uniform_real_distribution<float> valueDist(-2.0f, 2.0f);
    std::bernoulli_distribution zeroRow(0.1);

    const size_t dimensions = dimDist(rng);
    std::vector<float> left(dimensions);
    std::vector<float> right(dimensions);
    for (float &component : left)
    {
        component = zeroRow(rng) ? 0.0f : valueDist(rng);
    }
    for (float &component : right)
    {
        component = zeroRow(rng) ? 0.0f : valueDist(rng);
    }

    const auto expected = naiveCosine(left, right);
    const auto actual = CosineSimilarity::calculate<float>(left, right);
    REQUIRE(actual.has_value());
    REQUIRE(actual.value() == Catch::Approx(expected).margin(1e-5f));
}

TEST_CASE("searchTopKCosine matches brute-force ranking oracle", "[VectorSearch][oracle][Unit]")
{
    auto seed = GENERATE(take(24, random(1, 1000000)));
    std::mt19937 rng(static_cast<std::mt19937::result_type>(seed));
    std::uniform_int_distribution<uint64_t> dimDist(1, 16);
    std::uniform_int_distribution<size_t> countDist(1, 32);
    std::uniform_real_distribution<float> valueDist(-2.0f, 2.0f);
    std::bernoulli_distribution zeroRow(0.08);

    const uint64_t dimensions = dimDist(rng);
    const size_t vectorCount = countDist(rng);
    std::uniform_int_distribution<size_t> kDist(0, vectorCount + 2);
    const size_t k = kDist(rng);

    std::vector<std::vector<float>> corpus(vectorCount, std::vector<float>(static_cast<size_t>(dimensions)));
    std::vector<float> query(static_cast<size_t>(dimensions));
    for (auto &vector : corpus)
    {
        if (zeroRow(rng))
        {
            std::fill(vector.begin(), vector.end(), 0.0f);
            continue;
        }
        for (float &component : vector)
        {
            component = valueDist(rng);
        }
    }
    if (zeroRow(rng))
    {
        std::fill(query.begin(), query.end(), 0.0f);
    }
    else
    {
        for (float &component : query)
        {
            component = valueDist(rng);
        }
    }

    const auto dbPath = makeTempDbPath();
    TestDatabaseGuard guard(dbPath);
    StorageEngine<float> engine;
    REQUIRE(engine.createOrOpen(dbPath, dimensions).has_value());
    for (const auto &vector : corpus)
    {
        REQUIRE(engine.appendVector(vector).has_value());
    }

    const auto actual = searchTopKCosine<float>(engine, query, k);
    REQUIRE(actual.has_value());

    const size_t expectedK = k == 0 ? 0 : std::min(k, corpus.size());
    const auto expected = naiveTopK(corpus, query, expectedK);
    REQUIRE(actual->size() == expected.size());
    for (size_t index = 0; index < expected.size(); ++index)
    {
        REQUIRE((*actual)[index].index == expected[index].index);
        REQUIRE((*actual)[index].score == Catch::Approx(expected[index].score).margin(1e-5f));
    }
}

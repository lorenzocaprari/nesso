#include <array>
#include <catch2/catch_all.hpp>
#include <core/core_types.hpp>
#include <core/storage_engine.hpp>
#include <core/vector_search.hpp>
#include <cstdlib>
#include <filesystem>

namespace fs = std::filesystem;
using namespace core;

namespace
{

class TestDatabaseGuard
{
  public:
    explicit TestDatabaseGuard(const fs::path &path) : m_path(path) {}
    ~TestDatabaseGuard() { fs::remove(m_path); }

  private:
    fs::path m_path;
};

fs::path makeTempDbPath() { return fs::temp_directory_path() / ("vector_search_test_" + std::to_string(std::rand())); }

} // namespace

SCENARIO("searchTopKCosine ranks vectors by cosine similarity", "[VectorSearch][core][Unit]")
{
    const auto dbPath = makeTempDbPath();
    TestDatabaseGuard guard(dbPath);
    StorageEngine<float> engine;
    REQUIRE(engine.createOrOpen(dbPath, 2).has_value());

    const std::array<float, 2> first = {1.0f, 0.0f};
    const std::array<float, 2> tied = {2.0f, 0.0f};
    const std::array<float, 2> orthogonal = {0.0f, 1.0f};
    const std::array<float, 2> opposite = {-1.0f, 0.0f};
    const std::array<float, 2> zero = {0.0f, 0.0f};
    REQUIRE(engine.appendVector(first).has_value());
    REQUIRE(engine.appendVector(tied).has_value());
    REQUIRE(engine.appendVector(orthogonal).has_value());
    REQUIRE(engine.appendVector(opposite).has_value());
    REQUIRE(engine.appendVector(zero).has_value());

    const std::array<float, 2> query = {1.0f, 0.0f};
    auto results = searchTopKCosine<float>(engine, query, 3);

    REQUIRE(results.has_value());
    REQUIRE(results->size() == 3);
    REQUIRE((*results)[0].index == 0);
    REQUIRE((*results)[0].score == Catch::Approx(1.0f));
    REQUIRE((*results)[1].index == 1);
    REQUIRE((*results)[1].score == Catch::Approx(1.0f));
    REQUIRE((*results)[2].index == 2);
    REQUIRE((*results)[2].score == Catch::Approx(0.0f));
}

SCENARIO("searchTopKCosine handles boundary and invalid inputs", "[VectorSearch][core][Unit]")
{
    GIVEN("An unopened engine")
    {
        StorageEngine<float> engine;
        const std::array<float, 2> query = {1.0f, 0.0f};

        auto results = searchTopKCosine<float>(engine, query, 1);

        THEN("search fails with DatabaseNotInitialized")
        {
            REQUIRE_FALSE(results.has_value());
            REQUIRE(results.error() == EngineError::DatabaseNotInitialized);
        }
    }

    GIVEN("An empty, open database")
    {
        const auto dbPath = makeTempDbPath();
        TestDatabaseGuard guard(dbPath);
        StorageEngine<float> engine;
        REQUIRE(engine.createOrOpen(dbPath, 2).has_value());

        const std::array<float, 2> query = {1.0f, 0.0f};

        THEN("a zero top-K request returns no results")
        {
            auto results = searchTopKCosine<float>(engine, query, 0);
            REQUIRE(results.has_value());
            REQUIRE(results->empty());
        }

        THEN("searching an empty database returns no results")
        {
            auto results = searchTopKCosine<float>(engine, query, 1);
            REQUIRE(results.has_value());
            REQUIRE(results->empty());
        }

        THEN("a wrong-dimensional query fails")
        {
            const std::array<float, 1> wrongQuery = {1.0f};
            auto results = searchTopKCosine<float>(engine, wrongQuery, 1);
            REQUIRE_FALSE(results.has_value());
            REQUIRE(results.error() == EngineError::MismatchedDimensions);
        }
    }
}

SCENARIO("searchTopKCosine searches persisted vectors after remapping", "[VectorSearch][core][Unit]")
{
    const auto dbPath = makeTempDbPath();
    TestDatabaseGuard guard(dbPath);
    constexpr uint64_t dimensions = 2;
    constexpr size_t vectorCount = 1000;

    {
        StorageEngine<float> engine;
        REQUIRE(engine.createOrOpen(dbPath, dimensions).has_value());
        for (size_t index = 0; index < vectorCount; ++index)
        {
            const std::array<float, dimensions> vector = index + 1 == vectorCount
                                                             ? std::array<float, dimensions>{0.0f, 1.0f}
                                                             : std::array<float, dimensions>{1.0f, 0.0f};
            REQUIRE(engine.appendVector(vector).has_value());
        }
    }

    StorageEngine<float> reopened;
    REQUIRE(reopened.createOrOpen(dbPath, dimensions).has_value());
    const std::array<float, dimensions> query = {0.0f, 1.0f};
    auto results = searchTopKCosine<float>(reopened, query, vectorCount + 1);

    REQUIRE(results.has_value());
    REQUIRE(results->size() == vectorCount);
    REQUIRE((*results)[0].index == vectorCount - 1);
    REQUIRE((*results)[0].score == Catch::Approx(1.0f));
}

#include <array>
#include <catch2/catch_all.hpp>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mach_core/core_types.hpp>
#include <mach_core/storage_engine.hpp>
#include <vector>

namespace fs = std::filesystem;
using namespace mach_core;

class TestDatabaseGuard
{
  public:
    explicit TestDatabaseGuard(const fs::path &path) : m_path(path) {}
    ~TestDatabaseGuard() { fs::remove(m_path); }

  private:
    fs::path m_path;
};

namespace
{
fs::path makeTempDbPath() { return fs::temp_directory_path() / ("test_db_" + std::to_string(std::rand())); }
} // namespace

SCENARIO("StorageEngine persists and retrieves vectors", "[StorageEngine][core][Unit]")
{
    GIVEN("A storage engine initialized with a temporary database file")
    {
        const auto db_path = fs::temp_directory_path() / ("test_db_" + std::to_string(std::rand()));
        TestDatabaseGuard guard(db_path);

        StorageEngine<float> engine;
        constexpr uint64_t dimensions = 3;

        WHEN("creating a new database with 3 dimensions")
        {
            auto result = engine.createOrOpen(db_path, dimensions);

            THEN("the operation succeeds") { REQUIRE(result.has_value()); }

            THEN("the engine reports correct dimensions") { REQUIRE(engine.getDimensions() == dimensions); }

            THEN("the engine initially contains zero vectors") { REQUIRE(engine.getVectorCount() == 0); }

            AND_WHEN("appending a single vector")
            {
                std::array<float, 3> test_vector = {1.0f, 2.0f, 3.0f};
                auto append_result = engine.appendVector(test_vector);

                // Assert that the operation was a success
                REQUIRE(append_result.has_value());

                AND_WHEN("retrieving the first vector at index 0")
                {
                    auto retrieved = engine.getVector(0);

                    THEN("the operation succeeds") { REQUIRE(retrieved.has_value()); }

                    THEN("the retrieved vector matches the original")
                    {
                        auto vec = retrieved.value();
                        REQUIRE(vec.size() == 3);
                        REQUIRE(vec[0] == 1.0f);
                        REQUIRE(vec[1] == 2.0f);
                        REQUIRE(vec[2] == 3.0f);
                    }
                }
            }
        }
    }
}

SCENARIO("StorageEngine rejects invalid operations", "[StorageEngine][core][Unit]")
{
    GIVEN("A default-constructed engine that has never been opened")
    {
        StorageEngine<float> engine;

        THEN("its dimensions and vector count report as zero")
        {
            REQUIRE(engine.getDimensions() == 0);
            REQUIRE(engine.getVectorCount() == 0);
        }

        THEN("appending a vector fails with DatabaseNotInitialized")
        {
            std::array<float, 3> vec = {1.0f, 2.0f, 3.0f};
            auto result = engine.appendVector(vec);
            REQUIRE_FALSE(result.has_value());
            REQUIRE(result.error() == EngineError::DatabaseNotInitialized);
        }

        THEN("fetching a vector fails with DatabaseNotInitialized")
        {
            auto result = engine.getVector(0);
            REQUIRE_FALSE(result.has_value());
            REQUIRE(result.error() == EngineError::DatabaseNotInitialized);
        }
    }

    GIVEN("An open database with 3 dimensions")
    {
        const auto db_path = makeTempDbPath();
        TestDatabaseGuard guard(db_path);

        StorageEngine<float> engine;
        constexpr uint64_t dimensions = 3;
        REQUIRE(engine.createOrOpen(db_path, dimensions).has_value());

        WHEN("appending a vector with the wrong number of elements")
        {
            std::array<float, 2> wrongSizeVector = {1.0f, 2.0f};
            auto result = engine.appendVector(wrongSizeVector);

            THEN("the operation fails with MismatchedDimensions")
            {
                REQUIRE_FALSE(result.has_value());
                REQUIRE(result.error() == EngineError::MismatchedDimensions);
            }
        }

        WHEN("fetching an index beyond the current vector count")
        {
            std::array<float, 3> vec = {1.0f, 2.0f, 3.0f};
            REQUIRE(engine.appendVector(vec).has_value());

            auto result = engine.getVector(5);

            THEN("the operation fails with IndexOutOfBounds")
            {
                REQUIRE_FALSE(result.has_value());
                REQUIRE(result.error() == EngineError::IndexOutOfBounds);
            }
        }
    }
}

SCENARIO("StorageEngine rejects corrupt on-disk headers", "[StorageEngine][core][Unit]")
{
    GIVEN("A truncated file smaller than DatabaseHeader")
    {
        const auto db_path = makeTempDbPath();
        TestDatabaseGuard guard(db_path);
        {
            std::ofstream out(db_path, std::ios::binary);
            const char bytes[] = {'M', 'A', 'C', 'H'};
            out.write(bytes, 4);
        }

        StorageEngine<float> engine;
        auto result = engine.createOrOpen(db_path, 2);

        THEN("open fails with CorruptDatabase")
        {
            REQUIRE_FALSE(result.has_value());
            REQUIRE(result.error() == EngineError::CorruptDatabase);
        }
    }

    GIVEN("A full-size header with invalid magic")
    {
        const auto db_path = makeTempDbPath();
        TestDatabaseGuard guard(db_path);
        {
            DatabaseHeader header{};
            header.magic = {'B', 'A', 'D', '!'};
            header.version = 1;
            header.dimensions = 2;
            header.vector_count = 0;
            std::ofstream out(db_path, std::ios::binary);
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
            out.write(reinterpret_cast<const char *>(&header), sizeof(header));
        }

        StorageEngine<float> engine;
        auto result = engine.createOrOpen(db_path, 2);

        THEN("open fails with CorruptDatabase")
        {
            REQUIRE_FALSE(result.has_value());
            REQUIRE(result.error() == EngineError::CorruptDatabase);
        }
    }

    GIVEN("A header claiming more vectors than the file can hold")
    {
        const auto db_path = makeTempDbPath();
        TestDatabaseGuard guard(db_path);
        {
            DatabaseHeader header{};
            header.magic = {'M', 'A', 'C', 'H'};
            header.version = 1;
            header.dimensions = 2;
            header.vector_count = 1000000;
            std::ofstream out(db_path, std::ios::binary);
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
            out.write(reinterpret_cast<const char *>(&header), sizeof(header));
        }

        StorageEngine<float> engine;
        auto result = engine.createOrOpen(db_path, 2);

        THEN("open fails with CorruptDatabase")
        {
            REQUIRE_FALSE(result.has_value());
            REQUIRE(result.error() == EngineError::CorruptDatabase);
        }
    }
}

SCENARIO("StorageEngine supports reopening an existing database", "[StorageEngine][core][Unit]")
{
    GIVEN("A database file created and populated with 2 vectors of 3 dimensions")
    {
        const auto db_path = makeTempDbPath();
        TestDatabaseGuard guard(db_path);
        constexpr uint64_t dimensions = 3;

        {
            StorageEngine<float> engine;
            REQUIRE(engine.createOrOpen(db_path, dimensions).has_value());
            std::array<float, 3> first = {1.0f, 2.0f, 3.0f};
            std::array<float, 3> second = {4.0f, 5.0f, 6.0f};
            REQUIRE(engine.appendVector(first).has_value());
            REQUIRE(engine.appendVector(second).has_value());
        }

        WHEN("reopening the file with the same dimensions")
        {
            StorageEngine<float> reopened;
            auto result = reopened.createOrOpen(db_path, dimensions);

            THEN("the operation succeeds") { REQUIRE(result.has_value()); }

            THEN("the previously persisted vector count is preserved") { REQUIRE(reopened.getVectorCount() == 2); }

            THEN("previously written vectors are still readable")
            {
                auto vec0 = reopened.getVector(0);
                auto vec1 = reopened.getVector(1);
                REQUIRE(vec0.has_value());
                REQUIRE(vec1.has_value());
                REQUIRE(vec0.value()[0] == 1.0f);
                REQUIRE(vec1.value()[0] == 4.0f);
            }
        }

        WHEN("reopening the file with a different dimensionality")
        {
            StorageEngine<float> reopened;
            auto result = reopened.createOrOpen(db_path, dimensions + 1);

            THEN("the operation fails with MismatchedDimensions")
            {
                REQUIRE_FALSE(result.has_value());
                REQUIRE(result.error() == EngineError::MismatchedDimensions);
            }
        }
    }
}

SCENARIO("StorageEngine correctly grows its backing mapping across many appends", "[StorageEngine][core][Unit]")
{
    GIVEN("A freshly created database with a small dimensionality")
    {
        const auto db_path = makeTempDbPath();
        TestDatabaseGuard guard(db_path);
        constexpr uint64_t dimensions = 4;

        StorageEngine<float> engine;
        REQUIRE(engine.createOrOpen(db_path, dimensions).has_value());

        WHEN("appending enough vectors to force multiple mmap growth cycles")
        {
            constexpr size_t vectorCountToInsert = 2000;
            for (size_t i = 0; i < vectorCountToInsert; ++i)
            {
                std::array<float, dimensions> vec = {static_cast<float>(i), static_cast<float>(i) + 1.0f,
                                                     static_cast<float>(i) + 2.0f, static_cast<float>(i) + 3.0f};
                REQUIRE(engine.appendVector(vec).has_value());
            }

            THEN("the reported vector count matches the number inserted")
            {
                REQUIRE(engine.getVectorCount() == vectorCountToInsert);
            }

            THEN("every inserted vector remains correctly readable")
            {
                for (size_t i = 0; i < vectorCountToInsert; ++i)
                {
                    auto retrieved = engine.getVector(i);
                    REQUIRE(retrieved.has_value());
                    auto vec = retrieved.value();
                    REQUIRE(vec[0] == static_cast<float>(i));
                    REQUIRE(vec[3] == static_cast<float>(i) + 3.0f);
                }
            }
        }
    }
}

SCENARIO("StorageEngine supports move construction and move assignment", "[StorageEngine][core][Unit]")
{
    GIVEN("An open database populated with a single vector")
    {
        const auto db_path = makeTempDbPath();
        TestDatabaseGuard guard(db_path);
        constexpr uint64_t dimensions = 3;

        StorageEngine<float> original;
        REQUIRE(original.createOrOpen(db_path, dimensions).has_value());
        std::array<float, 3> vec = {1.0f, 2.0f, 3.0f};
        REQUIRE(original.appendVector(vec).has_value());

        WHEN("move-constructing a new engine from the original")
        {
            StorageEngine<float> moved(std::move(original));

            THEN("the new engine takes over the database state")
            {
                REQUIRE(moved.getDimensions() == dimensions);
                REQUIRE(moved.getVectorCount() == 1);
                auto retrieved = moved.getVector(0);
                REQUIRE(retrieved.has_value());
                REQUIRE(retrieved.value()[0] == 1.0f);
            }

            THEN("the moved-from engine is left inert")
            {
                REQUIRE(original.getDimensions() == 0);
                REQUIRE(original.getVectorCount() == 0);
                auto result = original.getVector(0);
                REQUIRE_FALSE(result.has_value());
                REQUIRE(result.error() == EngineError::DatabaseNotInitialized);
            }
        }

        WHEN("move-assigning into an already-open engine")
        {
            const auto other_db_path = makeTempDbPath();
            TestDatabaseGuard otherGuard(other_db_path);

            StorageEngine<float> target;
            REQUIRE(target.createOrOpen(other_db_path, dimensions).has_value());

            target = std::move(original);

            THEN("the target engine takes over the original database state")
            {
                REQUIRE(target.getDimensions() == dimensions);
                REQUIRE(target.getVectorCount() == 1);
                auto retrieved = target.getVector(0);
                REQUIRE(retrieved.has_value());
                REQUIRE(retrieved.value()[0] == 1.0f);
            }

            THEN("the moved-from engine is left inert")
            {
                REQUIRE(original.getDimensions() == 0);
                REQUIRE(original.getVectorCount() == 0);
            }
        }
    }
}

#include <array>
#include <catch2/catch_all.hpp>
#include <cstdlib>
#include <filesystem>
#include <mach_core/core_types.hpp>
#include <mach_core/storage_engine.hpp>

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

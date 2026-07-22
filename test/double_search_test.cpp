#include <array>
#include <cstdlib>
#include <filesystem>
#include <mach_core/storage_engine.hpp>
#include <mach_core/vector_search.hpp>

int main()
{
    const auto databasePath = std::filesystem::temp_directory_path() / "mach1_double_search_test.mach1";
    std::filesystem::remove(databasePath);

    mach_core::StorageEngine<double> engine;
    const std::array<double, 2> vector = {1.0, 0.0};
    const std::array<double, 2> query = {1.0, 0.0};
    const auto created = engine.createOrOpen(databasePath, 2);
    const auto appended = created ? engine.appendVector(vector) : std::unexpected(created.error());
    const auto results =
        appended ? mach_core::searchTopKCosine<double>(engine, query, 1) : std::unexpected(appended.error());
    const bool passed = results && results->size() == 1 && results->front().index == 0 && results->front().score == 1.0;

    std::filesystem::remove(databasePath);
    return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}

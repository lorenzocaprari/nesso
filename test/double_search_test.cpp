#include <array>
#include <core/storage_engine.hpp>
#include <core/vector_search.hpp>
#include <cstdlib>
#include <filesystem>

int main()
{
    const auto databasePath = std::filesystem::temp_directory_path() / "nesso_double_search_test.nesso";
    std::filesystem::remove(databasePath);

    core::StorageEngine<double> engine;
    const std::array<double, 2> vector = {1.0, 0.0};
    const std::array<double, 2> query = {1.0, 0.0};
    const auto created = engine.createOrOpen(databasePath, 2);
    const auto appended = created ? engine.appendVector(vector) : std::unexpected(created.error());
    const auto results =
        appended ? core::searchTopKCosine<double>(engine, query, 1) : std::unexpected(appended.error());
    const bool passed = results && results->size() == 1 && results->front().index == 0 && results->front().score == 1.0;

    std::filesystem::remove(databasePath);
    return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}

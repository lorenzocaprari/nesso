#include <array>
#include <catch2/catch_all.hpp>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <mach_core/core_types.hpp>
#include <mach_core/distance.hpp>
#include <mach_core/storage_engine.hpp>
#include <mach_core/vector_index.hpp>
#include <numeric>
#include <span>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using namespace mach_core;
using mach_core::math::CosineSimilarity;

namespace
{
fs::path makeTempDbPath() { return fs::temp_directory_path() / ("vector_index_test_" + std::to_string(std::rand())); }

class TestDatabaseGuard
{
  public:
    explicit TestDatabaseGuard(const fs::path &path) : m_path(path) {}
    ~TestDatabaseGuard() { fs::remove(m_path); }

  private:
    fs::path m_path;
};
} // namespace

TEST_CASE("AVX2 cosine matches the scalar path on 384-d vectors", "[Distance][simd][Unit]")
{
    std::vector<float> a(kEmbeddingDims);
    std::vector<float> b(kEmbeddingDims);
    std::iota(a.begin(), a.end(), 0.25f);
    std::iota(b.begin(), b.end(), 1.5f);
    for (float &v : b)
    {
        v = -v;
    }

    auto scalar = CosineSimilarity::calculateScalar<float>(a, b);
    auto dispatched = CosineSimilarity::calculate<float>(a, b);
    REQUIRE(scalar.has_value());
    REQUIRE(dispatched.has_value());
    REQUIRE(*dispatched == Catch::Approx(*scalar).margin(1e-5f));
}

TEST_CASE("VectorIndex exposes an mdspan over stored rows", "[VectorIndex][core][Unit]")
{
    const auto dbPath = makeTempDbPath();
    TestDatabaseGuard guard(dbPath);
    StorageEngine<float> engine;
    REQUIRE(engine.createOrOpen(dbPath, 4).has_value());
    const std::array<float, 4> row{1.0f, 2.0f, 3.0f, 4.0f};
    REQUIRE(engine.appendVector(row).has_value());

    const VectorIndex index(engine);
    REQUIRE(index.rows() == 1);
    REQUIRE(index.cols() == 4);
    REQUIRE(index.asMdspan()[0, 2] == 3.0f);
}

TEST_CASE("cosine benchmark scalar vs dispatched 10k x 384", "[Distance][bench]")
{
    constexpr size_t rows = 10000;
    std::vector<float> corpus(rows * kEmbeddingDims, 0.1f);
    std::vector<float> query(kEmbeddingDims, 0.2f);
    for (size_t i = 0; i < corpus.size(); ++i)
    {
        corpus[i] = static_cast<float>(i % 17) * 0.01f;
    }

    const auto run = [&](auto fn)
    {
        const auto start = std::chrono::steady_clock::now();
        float acc = 0.0f;
        for (size_t r = 0; r < rows; ++r)
        {
            std::span<const float> row{corpus.data() + r * kEmbeddingDims, kEmbeddingDims};
            auto score = fn(query, row);
            acc += score.value_or(0.0f);
        }
        const auto elapsed = std::chrono::steady_clock::now() - start;
        return std::pair{elapsed, acc};
    };

    auto scalar = run([](std::span<const float> a, std::span<const float> b)
                      { return CosineSimilarity::calculateScalar<float>(a, b); });
    auto dispatched = run([](std::span<const float> a, std::span<const float> b)
                          { return CosineSimilarity::calculate<float>(a, b); });

    WARN("scalar_ns=" << scalar.first.count() << " dispatched_ns=" << dispatched.first.count());
    REQUIRE(scalar.second == Catch::Approx(dispatched.second).margin(1.0f));
}

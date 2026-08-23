#include <array>
#include <catch2/catch_all.hpp>
#include <core/core_types.hpp>
#include <core/distance.hpp>
#include <random>
#include <vector>

using namespace core;
using core::math::CosineSimilarity;
using core::math::DistanceMetrics;

TEMPLATE_TEST_CASE("CosineSimilarity handles well-formed vectors", "[CosineSimilarity][core][Unit]", float, double)
{
    GIVEN("Two identical vectors")
    {
        std::array<TestType, 3> a = {1.0, 2.0, 3.0};
        std::array<TestType, 3> b = {1.0, 2.0, 3.0};

        WHEN("computing the cosine similarity")
        {
            auto result = CosineSimilarity::calculate<TestType>(a, b);

            THEN("the operation succeeds and reports maximal similarity")
            {
                REQUIRE(result.has_value());
                REQUIRE(result.value() == Catch::Approx(1.0));
            }
        }
    }

    GIVEN("Two orthogonal vectors")
    {
        std::array<TestType, 2> a = {1.0, 0.0};
        std::array<TestType, 2> b = {0.0, 1.0};

        WHEN("computing the cosine similarity")
        {
            auto result = CosineSimilarity::calculate<TestType>(a, b);

            THEN("the operation succeeds and reports zero similarity")
            {
                REQUIRE(result.has_value());
                REQUIRE(result.value() == Catch::Approx(0.0));
            }
        }
    }

    GIVEN("Two diametrically opposed vectors")
    {
        std::array<TestType, 3> a = {1.0, 2.0, 3.0};
        std::array<TestType, 3> b = {-1.0, -2.0, -3.0};

        WHEN("computing the cosine similarity")
        {
            auto result = CosineSimilarity::calculate<TestType>(a, b);

            THEN("the operation succeeds and reports minimal similarity")
            {
                REQUIRE(result.has_value());
                REQUIRE(result.value() == Catch::Approx(-1.0));
            }
        }
    }

    GIVEN("A zero vector paired with a non-zero vector")
    {
        std::array<TestType, 3> a = {0.0, 0.0, 0.0};
        std::array<TestType, 3> b = {1.0, 2.0, 3.0};

        WHEN("computing the cosine similarity")
        {
            auto result = CosineSimilarity::calculate<TestType>(a, b);

            THEN("the division-by-zero guard reports zero similarity instead of failing")
            {
                REQUIRE(result.has_value());
                REQUIRE(result.value() == Catch::Approx(0.0));
            }
        }
    }
}

TEMPLATE_TEST_CASE("CosineSimilarity rejects invalid inputs", "[CosineSimilarity][core][Unit]", float, double)
{
    GIVEN("Two vectors with mismatched dimensionality")
    {
        std::array<TestType, 3> a = {1.0, 2.0, 3.0};
        std::array<TestType, 2> b = {1.0, 2.0};

        WHEN("computing the cosine similarity")
        {
            auto result = CosineSimilarity::calculate<TestType>(a, b);

            THEN("the operation fails with MismatchedDimensions")
            {
                REQUIRE_FALSE(result.has_value());
                REQUIRE(result.error() == EngineError::MismatchedDimensions);
            }
        }
    }

    GIVEN("Two empty vectors")
    {
        std::span<const TestType> a{};
        std::span<const TestType> b{};

        WHEN("computing the cosine similarity")
        {
            auto result = CosineSimilarity::calculate<TestType>(a, b);

            THEN("the operation fails rather than dividing by zero") { REQUIRE_FALSE(result.has_value()); }
        }
    }
}

TEMPLATE_TEST_CASE("DistanceMetrics::dotProduct computes known values", "[DistanceMetrics][core][Unit]", float, double)
{
    GIVEN("Two aligned vectors")
    {
        std::array<TestType, 3> a = {1.0, 2.0, 3.0};
        std::array<TestType, 3> b = {4.0, 5.0, 6.0};

        WHEN("computing the dot product")
        {
            auto result = DistanceMetrics::dotProduct<TestType>(a, b);

            THEN("the operation succeeds with the expected sum")
            {
                REQUIRE(result.has_value());
                REQUIRE(result.value() == Catch::Approx(32.0));
            }
        }
    }
}

TEMPLATE_TEST_CASE("DistanceMetrics::dotProduct rejects invalid inputs", "[DistanceMetrics][core][Unit]", float, double)
{
    GIVEN("Two vectors with mismatched dimensionality")
    {
        std::array<TestType, 3> a = {1.0, 2.0, 3.0};
        std::array<TestType, 2> b = {1.0, 2.0};

        WHEN("computing the dot product")
        {
            auto result = DistanceMetrics::dotProduct<TestType>(a, b);

            THEN("the operation fails with MismatchedDimensions")
            {
                REQUIRE_FALSE(result.has_value());
                REQUIRE(result.error() == EngineError::MismatchedDimensions);
            }
        }
    }

    GIVEN("Two empty vectors")
    {
        std::span<const TestType> a{};
        std::span<const TestType> b{};

        WHEN("computing the dot product")
        {
            auto result = DistanceMetrics::dotProduct<TestType>(a, b);

            THEN("the operation fails") { REQUIRE_FALSE(result.has_value()); }
        }
    }
}

TEMPLATE_TEST_CASE("DistanceMetrics::l2SquaredDistance computes known values", "[DistanceMetrics][core][Unit]", float,
                   double)
{
    GIVEN("Two vectors with a known delta")
    {
        std::array<TestType, 3> a = {1.0, 2.0, 3.0};
        std::array<TestType, 3> b = {2.0, 4.0, 6.0};

        WHEN("computing the squared L2 distance")
        {
            auto result = DistanceMetrics::l2SquaredDistance<TestType>(a, b);

            THEN("the operation succeeds with the expected sum of squares")
            {
                REQUIRE(result.has_value());
                REQUIRE(result.value() == Catch::Approx(14.0));
            }
        }
    }
}

TEMPLATE_TEST_CASE("DistanceMetrics::l2SquaredDistance rejects invalid inputs", "[DistanceMetrics][core][Unit]", float,
                   double)
{
    GIVEN("Two vectors with mismatched dimensionality")
    {
        std::array<TestType, 3> a = {1.0, 2.0, 3.0};
        std::array<TestType, 2> b = {1.0, 2.0};

        WHEN("computing the squared L2 distance")
        {
            auto result = DistanceMetrics::l2SquaredDistance<TestType>(a, b);

            THEN("the operation fails with MismatchedDimensions")
            {
                REQUIRE_FALSE(result.has_value());
                REQUIRE(result.error() == EngineError::MismatchedDimensions);
            }
        }
    }

    GIVEN("Two empty vectors")
    {
        std::span<const TestType> a{};
        std::span<const TestType> b{};

        WHEN("computing the squared L2 distance")
        {
            auto result = DistanceMetrics::l2SquaredDistance<TestType>(a, b);

            THEN("the operation fails") { REQUIRE_FALSE(result.has_value()); }
        }
    }
}

#ifdef __AVX2__
TEST_CASE("DistanceMetrics AVX2 dotProduct matches scalar oracle", "[DistanceMetrics][core][Unit][avx2]")
{
    std::mt19937 rng{0xC0FFEEU};
    std::uniform_real_distribution<float> dist(-10.0F, 10.0F);

    for (const size_t size : {1U, 7U, 8U, 9U, 16U, 31U, 64U})
    {
        std::vector<float> a(size);
        std::vector<float> b(size);
        for (size_t i = 0; i < size; ++i)
        {
            a[i] = dist(rng);
            b[i] = dist(rng);
        }

        const std::span<const float> aSpan{a.data(), a.size()};
        const std::span<const float> bSpan{b.data(), b.size()};
        const float scalar = core::math::detail::dotProductScalar(aSpan, bSpan);
        const float avx2 = core::math::detail::dotProductAvx2(aSpan, bSpan);
        REQUIRE(avx2 == Catch::Approx(scalar).margin(1e-4F));
    }
}

TEST_CASE("DistanceMetrics AVX2 l2SquaredDistance matches scalar oracle", "[DistanceMetrics][core][Unit][avx2]")
{
    std::mt19937 rng{0xBEEFU};
    std::uniform_real_distribution<float> dist(-10.0F, 10.0F);

    for (const size_t size : {1U, 7U, 8U, 9U, 16U, 31U, 64U})
    {
        std::vector<float> a(size);
        std::vector<float> b(size);
        for (size_t i = 0; i < size; ++i)
        {
            a[i] = dist(rng);
            b[i] = dist(rng);
        }

        const std::span<const float> aSpan{a.data(), a.size()};
        const std::span<const float> bSpan{b.data(), b.size()};
        const float scalar = core::math::detail::l2SquaredDistanceScalar(aSpan, bSpan);
        const float avx2 = core::math::detail::l2SquaredDistanceAvx2(aSpan, bSpan);
        REQUIRE(avx2 == Catch::Approx(scalar).margin(1e-4F));
    }
}
#endif

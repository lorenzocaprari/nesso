#include <array>
#include <catch2/catch_all.hpp>
#include <core/core_types.hpp>
#include <core/distance.hpp>
#include <random>
#include <span>
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

#if defined(__x86_64__)
#include "distance_kernels.hpp"

namespace
{

void fillPair(std::mt19937 &rng, std::vector<float> &left, std::vector<float> &right)
{
    std::uniform_real_distribution<float> dist(-10.0F, 10.0F);
    for (size_t index = 0; index < left.size(); ++index)
    {
        left[index] = dist(rng);
        right[index] = dist(rng);
    }
}

} // namespace

TEST_CASE("float kernels match the scalar oracle", "[DistanceMetrics][core][Unit][avx2]")
{
    if (!__builtin_cpu_supports("avx2") || !__builtin_cpu_supports("fma"))
    {
        SKIP("CPU lacks AVX2+FMA");
    }

    std::mt19937 rng{0xC0FFEEU};
    for (const size_t size : {1U, 7U, 8U, 9U, 16U, 31U, 32U, 33U, 64U, 384U})
    {
        std::vector<float> left(size);
        std::vector<float> right(size);
        fillPair(rng, left, right);
        const std::span<const float> leftSpan{left};
        const std::span<const float> rightSpan{right};

        const float scalarDot = core::math::detail::dotProductScalar<float>(leftSpan, rightSpan);
        const float fmaDot =
            core::math::detail::dotProductKernel(core::math::detail::DistanceKernel::Avx2Fma, leftSpan, rightSpan);
        const float forcedScalarDot =
            core::math::detail::dotProductKernel(core::math::detail::DistanceKernel::Scalar, leftSpan, rightSpan);
        REQUIRE(fmaDot == Catch::Approx(scalarDot).margin(1e-3F));
        REQUIRE(forcedScalarDot == Catch::Approx(scalarDot).margin(1e-3F));

        const float scalarL2 = core::math::detail::l2SquaredDistanceScalar<float>(leftSpan, rightSpan);
        const float fmaL2 = core::math::detail::l2SquaredDistanceKernel(core::math::detail::DistanceKernel::Avx2Fma,
                                                                        leftSpan, rightSpan);
        const float forcedScalarL2 = core::math::detail::l2SquaredDistanceKernel(
            core::math::detail::DistanceKernel::Scalar, leftSpan, rightSpan);
        REQUIRE(fmaL2 == Catch::Approx(scalarL2).margin(1e-3F));
        REQUIRE(forcedScalarL2 == Catch::Approx(scalarL2).margin(1e-3F));

        const auto dispatched = DistanceMetrics::dotProduct<float>(leftSpan, rightSpan);
        REQUIRE(dispatched.has_value());
        REQUIRE(dispatched.value() == Catch::Approx(scalarDot).margin(1e-3F));
    }
}
#endif

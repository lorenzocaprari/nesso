#include <array>
#include <catch2/catch_all.hpp>
#include <core/core_types.hpp>
#include <core/distance.hpp>
#include <random>
#include <span>
#include <vector>

using namespace nesso::core;
using nesso::core::math::CosineSimilarity;
using nesso::core::math::DistanceMetrics;

TEMPLATE_TEST_CASE("CosineSimilarity handles well-formed vectors", "[CosineSimilarity][core][Unit]", float, double)
{
    GIVEN("Two identical vectors")
    {
        std::array<TestType, 3> a = {1.0, 2.0, 3.0};
        std::array<TestType, 3> b = {1.0, 2.0, 3.0};

        WHEN("computing the cosine similarity")
        {
            auto result = CosineSimilarity::Calculate<TestType>(a, b);

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
            auto result = CosineSimilarity::Calculate<TestType>(a, b);

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
            auto result = CosineSimilarity::Calculate<TestType>(a, b);

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
            auto result = CosineSimilarity::Calculate<TestType>(a, b);

            THEN("the division-by-zero guard reports zero similarity instead of "
                 "failing")
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
            auto result = CosineSimilarity::Calculate<TestType>(a, b);

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
            auto result = CosineSimilarity::Calculate<TestType>(a, b);

            THEN("the operation fails rather than dividing by zero") { REQUIRE_FALSE(result.has_value()); }
        }
    }
}

TEMPLATE_TEST_CASE("DistanceMetrics::DotProduct computes known values", "[DistanceMetrics][core][Unit]", float, double)
{
    GIVEN("Two aligned vectors")
    {
        std::array<TestType, 3> a = {1.0, 2.0, 3.0};
        std::array<TestType, 3> b = {4.0, 5.0, 6.0};

        WHEN("computing the dot product")
        {
            auto result = DistanceMetrics::DotProduct<TestType>(a, b);

            THEN("the operation succeeds with the expected sum")
            {
                REQUIRE(result.has_value());
                REQUIRE(result.value() == Catch::Approx(32.0));
            }
        }
    }
}

TEMPLATE_TEST_CASE("DistanceMetrics::DotProduct rejects invalid inputs", "[DistanceMetrics][core][Unit]", float, double)
{
    GIVEN("Two vectors with mismatched dimensionality")
    {
        std::array<TestType, 3> a = {1.0, 2.0, 3.0};
        std::array<TestType, 2> b = {1.0, 2.0};

        WHEN("computing the dot product")
        {
            auto result = DistanceMetrics::DotProduct<TestType>(a, b);

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
            auto result = DistanceMetrics::DotProduct<TestType>(a, b);

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
            auto result = DistanceMetrics::L2SquaredDistance<TestType>(a, b);

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
            auto result = DistanceMetrics::L2SquaredDistance<TestType>(a, b);

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
            auto result = DistanceMetrics::L2SquaredDistance<TestType>(a, b);

            THEN("the operation fails") { REQUIRE_FALSE(result.has_value()); }
        }
    }
}

#if defined(__x86_64__)
#include <core/distance_kernels.hpp>

namespace
{

void FillPair(std::mt19937 &rng, std::vector<float> &left, std::vector<float> &right)
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
        FillPair(rng, left, right);
        const std::span<const float> left_span{left};
        const std::span<const float> right_span{right};

        const float scalar_dot = nesso::core::math::detail::DotProductScalar<float>(left_span, right_span);
        const float fma_dot = nesso::core::math::detail::DotProductKernel(
            nesso::core::math::detail::DistanceKernel::Avx2Fma, left_span, right_span);
        const float forced_scalar_dot = nesso::core::math::detail::DotProductKernel(
            nesso::core::math::detail::DistanceKernel::Scalar, left_span, right_span);
        REQUIRE(fma_dot == Catch::Approx(scalar_dot).margin(1e-3F));
        REQUIRE(forced_scalar_dot == Catch::Approx(scalar_dot).margin(1e-3F));

        const float scalar_l2 = nesso::core::math::detail::L2SquaredDistanceScalar<float>(left_span, right_span);
        const float fma_l2 = nesso::core::math::detail::L2SquaredDistanceKernel(
            nesso::core::math::detail::DistanceKernel::Avx2Fma, left_span, right_span);
        const float forced_scalar_l2 = nesso::core::math::detail::L2SquaredDistanceKernel(
            nesso::core::math::detail::DistanceKernel::Scalar, left_span, right_span);
        REQUIRE(fma_l2 == Catch::Approx(scalar_l2).margin(1e-3F));
        REQUIRE(forced_scalar_l2 == Catch::Approx(scalar_l2).margin(1e-3F));

        const auto dispatched = DistanceMetrics::DotProduct<float>(left_span, right_span);
        REQUIRE(dispatched.has_value());
        REQUIRE(dispatched.value() == Catch::Approx(scalar_dot).margin(1e-3F));
    }
}
#endif

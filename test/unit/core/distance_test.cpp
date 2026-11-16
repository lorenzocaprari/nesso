#include <array>
#include <catch2/catch_all.hpp>
#include <core/core_types.hpp>
#include <core/distance.hpp>
#include <core/distance_kernels.hpp>
#include <cstdlib>
#include <random>
#include <span>
#include <string_view>
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

[[nodiscard]] bool HostHasAvx2Fma()
{
#if defined(__x86_64__)
    return __builtin_cpu_supports("avx2") && __builtin_cpu_supports("fma");
#else
    return false;
#endif
}

void RequireKernelMatchesScalar(nesso::core::math::detail::DistanceKernel kernel, std::span<const float> left,
                                std::span<const float> right)
{
    const float scalar_dot = nesso::core::math::detail::DotProductScalar<float>(left, right);
    const float got_dot = nesso::core::math::detail::DotProductKernel(kernel, left, right);
    REQUIRE(got_dot == Catch::Approx(scalar_dot).margin(1e-3F));

    const float scalar_l2 = nesso::core::math::detail::L2SquaredDistanceScalar<float>(left, right);
    const float got_l2 = nesso::core::math::detail::L2SquaredDistanceKernel(kernel, left, right);
    REQUIRE(got_l2 == Catch::Approx(scalar_l2).margin(1e-3F));
}

} // namespace

TEST_CASE("SelectDistanceKernel maps force values and CPU support", "[DistanceMetrics][core][Unit]")
{
    using nesso::core::math::detail::DistanceKernel;
    using nesso::core::math::detail::SelectDistanceKernel;

    REQUIRE(SelectDistanceKernel("scalar", true) == DistanceKernel::Scalar);
    REQUIRE(SelectDistanceKernel("scalar", false) == DistanceKernel::Scalar);

    REQUIRE(SelectDistanceKernel("avx2", true) == DistanceKernel::Avx2Fma);
    REQUIRE(SelectDistanceKernel("avx2", false) == DistanceKernel::Scalar);

    REQUIRE(SelectDistanceKernel({}, true) == DistanceKernel::Avx2Fma);
    REQUIRE(SelectDistanceKernel({}, false) == DistanceKernel::Scalar);
    REQUIRE(SelectDistanceKernel("", true) == DistanceKernel::Avx2Fma);
    REQUIRE(SelectDistanceKernel("", false) == DistanceKernel::Scalar);

    REQUIRE(SelectDistanceKernel("nope", true) == DistanceKernel::Avx2Fma);
    REQUIRE(SelectDistanceKernel("nope", false) == DistanceKernel::Scalar);
    REQUIRE(SelectDistanceKernel("AVX2", true) == DistanceKernel::Avx2Fma);
    REQUIRE(SelectDistanceKernel("scalar ", true) == DistanceKernel::Avx2Fma);
    REQUIRE(SelectDistanceKernel("nope", true) == SelectDistanceKernel("", true));
    REQUIRE(SelectDistanceKernel("nope", false) == SelectDistanceKernel("", false));
}

TEST_CASE("forced distance kernels match the scalar reference", "[DistanceMetrics][core][Unit]")
{
    using nesso::core::math::detail::DistanceKernel;
    using nesso::core::math::detail::SelectDistanceKernel;

    const bool avx2_fma = HostHasAvx2Fma();
    REQUIRE(SelectDistanceKernel("scalar", avx2_fma) == DistanceKernel::Scalar);
    if (avx2_fma)
    {
        REQUIRE(SelectDistanceKernel("avx2", avx2_fma) == DistanceKernel::Avx2Fma);
    }
    else
    {
        REQUIRE(SelectDistanceKernel("avx2", avx2_fma) == DistanceKernel::Scalar);
    }

    constexpr std::array<std::string_view, 4> forced_values{"scalar", "avx2", "", "nope"};
    std::mt19937 rng{0xC0FFEEU};
    for (const size_t size : {1U, 7U, 8U, 9U, 16U, 31U, 32U, 33U, 64U, 384U})
    {
        std::vector<float> left(size);
        std::vector<float> right(size);
        FillPair(rng, left, right);
        const std::span<const float> left_span{left};
        const std::span<const float> right_span{right};
        for (const std::string_view forced : forced_values)
        {
            RequireKernelMatchesScalar(SelectDistanceKernel(forced, avx2_fma), left_span, right_span);
        }
    }
}

TEST_CASE("SelectedKernel matches automatic selection when NESSO_FORCE_KERNEL is unset",
          "[DistanceMetrics][core][Unit]")
{
    const char *env_value = std::getenv("NESSO_FORCE_KERNEL");
    if (env_value != nullptr && env_value[0] != '\0')
    {
        SKIP("NESSO_FORCE_KERNEL is set");
    }

    REQUIRE(nesso::core::math::detail::SelectedKernel() ==
            nesso::core::math::detail::SelectDistanceKernel("", HostHasAvx2Fma()));
}

#if defined(__x86_64__)
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

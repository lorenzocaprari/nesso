// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_all.hpp>
#include <platform/narrow_cast.hpp>

#include <cstddef>
#include <limits>
#include <stdexcept>

TEST_CASE("NarrowCast accepts a value that fits", "[narrow-cast]")
{
    CHECK(nesso::platform::NarrowCast<int>(size_t{1024}) == 1024);
}

TEST_CASE("NarrowCast rejects a value that does not fit", "[narrow-cast]")
{
    constexpr size_t too_large = static_cast<size_t>(std::numeric_limits<int>::max()) + 1U;
    REQUIRE_THROWS_AS(nesso::platform::NarrowCast<int>(too_large), std::logic_error);
}

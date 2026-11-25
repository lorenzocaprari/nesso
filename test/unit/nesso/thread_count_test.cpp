// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_all.hpp>
#include <nesso_cli/thread_count.hpp>

#include <limits>
#include <string>

TEST_CASE("ThreadCount accepts 1 and 1024", "[thread-count]")
{
    const auto one = nesso::ThreadCount::Parse("1");
    REQUIRE(one);
    CHECK(one->Get() == 1);

    const auto max = nesso::ThreadCount::Parse("1024");
    REQUIRE(max);
    CHECK(max->Get() == 1024);
}

TEST_CASE("ThreadCount rejects 0 and values above 1024", "[thread-count]")
{
    const auto zero = nesso::ThreadCount::Parse("0");
    REQUIRE_FALSE(zero);
    CHECK(zero.error() == nesso::ThreadCountError::NotPositiveInteger);

    const auto over = nesso::ThreadCount::Parse("1025");
    REQUIRE_FALSE(over);
    CHECK(over.error() == nesso::ThreadCountError::OutOfRange);
}

TEST_CASE("ThreadCount rejects a digit string that does not fit in size_t", "[thread-count]")
{
    const std::string huge(std::numeric_limits<size_t>::digits10 + 3, '9');
    const auto parsed = nesso::ThreadCount::Parse(huge);
    REQUIRE_FALSE(parsed);
    CHECK(parsed.error() == nesso::ThreadCountError::NotPositiveInteger);
}

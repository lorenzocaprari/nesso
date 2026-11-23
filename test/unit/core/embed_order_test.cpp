// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_all.hpp>
#include <core/embed_order.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

using nesso::core::EmbedOrder;
using nesso::core::PromiseTerms;
using nesso::core::WindowPromise;

namespace
{

WindowPromise Promise(bool any_substr, std::uint32_t max_hits, std::uint32_t singletons)
{
    return WindowPromise{.any_substr = any_substr, .max_hits = max_hits, .singletons = singletons};
}

} // namespace

TEST_CASE("no windows give an empty order", "[embed_order]") { CHECK(EmbedOrder({}).empty()); }

TEST_CASE("zero keys keep input order", "[embed_order]")
{
    const std::vector<WindowPromise> windows(5);
    CHECK(EmbedOrder(windows) == std::vector<std::size_t>{0, 1, 2, 3, 4});
}

TEST_CASE("a substring match outranks every term and singleton count", "[embed_order]")
{
    const std::vector<WindowPromise> windows{Promise(false, 9, 1000), Promise(true, 0, 0), Promise(false, 9, 1001)};
    CHECK(EmbedOrder(windows) == std::vector<std::size_t>{1, 2, 0});
}

TEST_CASE("term hits outrank singletons", "[embed_order]")
{
    const std::vector<WindowPromise> windows{Promise(false, 1, 1024), Promise(false, 3, 0), Promise(false, 2, 7)};
    CHECK(EmbedOrder(windows) == std::vector<std::size_t>{1, 2, 0});
}

TEST_CASE("singletons break a tie on the first two keys", "[embed_order]")
{
    const std::vector<WindowPromise> windows{Promise(true, 2, 5), Promise(true, 2, 900), Promise(true, 2, 6)};
    CHECK(EmbedOrder(windows) == std::vector<std::size_t>{1, 2, 0});
}

TEST_CASE("equal windows keep ascending window index", "[embed_order]")
{
    const std::vector<WindowPromise> windows{Promise(false, 1, 3), Promise(true, 2, 4), Promise(false, 1, 3),
                                             Promise(true, 2, 4), Promise(false, 1, 3)};
    CHECK(EmbedOrder(windows) == std::vector<std::size_t>{1, 3, 0, 2, 4});
}

TEST_CASE("query terms count once each", "[embed_order]")
{
    const std::vector<std::string> query{"disk", "error", "disk"};
    const PromiseTerms terms{query};
    CHECK(terms.Size() == 2);
    CHECK(terms.Hits(std::vector<std::string>{"disk", "disk", "full"}) == 1);
    CHECK(terms.Hits(std::vector<std::string>{"error", "on", "disk"}) == 2);
    CHECK(terms.Hits(std::vector<std::string>{"nothing"}) == 0);
    CHECK(terms.Hits({}) == 0);
}

TEST_CASE("empty query terms never hit", "[embed_order]")
{
    const PromiseTerms terms;
    CHECK(terms.Size() == 0);
    CHECK(terms.Hits(std::vector<std::string>{"disk"}) == 0);
}

TEST_CASE("only the first 64 distinct query terms count", "[embed_order]")
{
    std::vector<std::string> query;
    for (std::size_t index = 0; index < nesso::core::PROMISE_TERM_LIMIT + 6; ++index)
    {
        query.push_back("t" + std::to_string(index));
        // Repeats do not use a slot.
        query.push_back("t0");
    }
    const PromiseTerms terms{query};
    CHECK(terms.Size() == nesso::core::PROMISE_TERM_LIMIT);
    CHECK(terms.Hits(query) == nesso::core::PROMISE_TERM_LIMIT);
    CHECK(terms.Hits(std::vector<std::string>{"t63"}) == 1);
    CHECK(terms.Hits(std::vector<std::string>{"t64", "t65", "t69"}) == 0);
    CHECK(terms.Hits(std::vector<std::string>{"t0", "t64", "t1"}) == 2);
}

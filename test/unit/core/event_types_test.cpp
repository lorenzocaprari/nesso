// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_all.hpp>
#include <core/dedup.hpp>
#include <core/event_types.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace
{

auto Line(std::string_view text, std::uint64_t line = 1) -> nesso::core::EventLine
{
    return {.source = "app.log", .line = line, .array_element = false, .text = text};
}

auto Stats(std::uint64_t count, std::uint64_t first_seen) -> nesso::core::EventTypeStats
{
    return {.key = std::to_string(first_seen),
            .count = count,
            .first_seen = first_seen,
            .last_seen = first_seen,
            .source = "app.log",
            .line = first_seen,
            .array_element = false,
            .text = std::to_string(first_seen)};
}

auto KeysOf(const nesso::core::EventTypeTally &tally, const std::vector<std::size_t> &order) -> std::vector<std::string>
{
    std::vector<std::string> keys;
    keys.reserve(order.size());
    for (const std::size_t index : order)
    {
        keys.push_back(tally.Types()[index].key);
    }
    return keys;
}

} // namespace

TEST_CASE("each line's type count is its dedup occurrence count", "[event_types]")
{
    const std::vector<std::string> lines{
        "PacketResponder 1 for block blk_38865049064139660 terminating",
        "Received block blk_-1608999687919862906 of size 91178 from /10.250.10.6",
        "PacketResponder 0 for block blk_-6952295868487656571 terminating",
        "sunny beach vacation",
        "PacketResponder 2 for block blk_7128370237687728475 terminating",
        "sunny beach vacation",
        "Received block blk_-1608999687919862906 of size 3 from /10.250.19.102",
    };
    std::vector<std::string_view> views(lines.begin(), lines.end());
    const std::vector<std::uint32_t> occurrences = nesso::core::OccurrenceCounts(views);

    nesso::core::EventTypeTally tally;
    // The type a line joined is the one whose last_seen is that line's ordinal.
    std::vector<std::uint64_t> counts;
    std::vector<std::size_t> joined;
    for (std::size_t index = 0; index < lines.size(); ++index)
    {
        REQUIRE(tally.Add(Line(lines[index], index + 1)));
        for (std::size_t type = 0; type < tally.Types().size(); ++type)
        {
            if (tally.Types()[type].last_seen == index + 1)
            {
                joined.push_back(type);
            }
        }
    }
    REQUIRE(joined.size() == lines.size());
    for (const std::size_t type : joined)
    {
        counts.push_back(tally.Types()[type].count);
    }
    CHECK(counts == std::vector<std::uint64_t>(occurrences.begin(), occurrences.end()));
    CHECK(tally.Lines() == lines.size());
    REQUIRE(tally.Types().size() == 3);

    const nesso::core::EventTypeStats &first = tally.Types()[0];
    CHECK(first.key == "PacketResponder <*> for block <*> terminating");
    CHECK(first.count == 3);
    CHECK(first.first_seen == 1);
    CHECK(first.last_seen == 5);
    CHECK(first.text == lines[0]);
    CHECK(first.line == 1);
    CHECK(first.source == "app.log");
    CHECK_FALSE(first.array_element);
    CHECK(tally.Types()[1].count == 2);
    CHECK(tally.Types()[1].first_seen == 2);
    CHECK(tally.Types()[1].last_seen == 7);
    CHECK(tally.Types()[2].key == "sunny beach vacation");
    CHECK(tally.Types()[2].count == 2);
}

TEST_CASE("a key function replaces the masked key", "[event_types]")
{
    nesso::core::EventTypeTally tally{4, [](std::string_view text) -> std::string
                                      { return std::string{text.substr(0, 1)}; }};
    REQUIRE(tally.Add(Line("a 1")));
    REQUIRE(tally.Add(Line("a 2")));
    REQUIRE(tally.Add({.source = "rows.json", .line = 3, .array_element = true, .text = "b 1"}));
    REQUIRE(tally.Types().size() == 2);
    CHECK(tally.Types()[0].key == "a");
    CHECK(tally.Types()[0].count == 2);
    CHECK(tally.Types()[1].key == "b");
    CHECK(tally.Types()[1].source == "rows.json");
    CHECK(tally.Types()[1].line == 3);
    CHECK(tally.Types()[1].array_element);
}

TEST_CASE("rare puts the fewest lines first and new the latest first appearance", "[event_types]")
{
    nesso::core::EventTypeTally tally;
    for (const std::string_view text : {"alpha", "beta", "alpha", "gamma", "alpha", "beta", "delta"})
    {
        REQUIRE(tally.Add(Line(text)));
    }

    const auto rare = nesso::core::RankEventTypes(tally.Types(), nesso::core::EventOrder::Rare, 10);
    CHECK(KeysOf(tally, rare) == std::vector<std::string>{"delta", "gamma", "beta", "alpha"});
    const auto fresh = nesso::core::RankEventTypes(tally.Types(), nesso::core::EventOrder::New, 10);
    CHECK(KeysOf(tally, fresh) == std::vector<std::string>{"delta", "gamma", "beta", "alpha"});

    CHECK(KeysOf(tally, nesso::core::RankEventTypes(tally.Types(), nesso::core::EventOrder::Rare, 2)) ==
          std::vector<std::string>{"delta", "gamma"});
    CHECK(nesso::core::RankEventTypes(tally.Types(), nesso::core::EventOrder::New, 0).empty());
    CHECK(nesso::core::RankEventTypes({}, nesso::core::EventOrder::Rare, 5).empty());
}

TEST_CASE("orders break ties on the other integer key", "[event_types]")
{
    // Two types with count 1: the one first seen later comes first.
    std::vector<nesso::core::EventTypeStats> stats{Stats(1, 1), Stats(3, 2), Stats(1, 4), Stats(2, 3)};
    CHECK(nesso::core::RankEventTypes(stats, nesso::core::EventOrder::Rare, 4) == std::vector<std::size_t>{2, 0, 3, 1});
    CHECK(nesso::core::RankEventTypes(stats, nesso::core::EventOrder::New, 4) == std::vector<std::size_t>{2, 3, 1, 0});

    // A tally gives every type its own first_seen. Hand-built stats may share
    // one, and then the smaller count comes first.
    stats = {Stats(5, 7), Stats(2, 7), Stats(9, 8)};
    CHECK(nesso::core::RankEventTypes(stats, nesso::core::EventOrder::New, 3) == std::vector<std::size_t>{2, 1, 0});
}

TEST_CASE("the tally refuses a new key past max_types", "[event_types]")
{
    nesso::core::EventTypeTally tally{2};
    REQUIRE(tally.Add(Line("one")));
    REQUIRE(tally.Add(Line("two")));
    CHECK_FALSE(tally.Add(Line("three")));
    CHECK(tally.Types().size() == 2);
    CHECK(tally.Lines() == 2);
    CHECK(tally.Add(Line("one")));
    CHECK(tally.Lines() == 3);
    CHECK(tally.Types()[0].count == 2);
    CHECK(tally.Types()[0].last_seen == 3);
}

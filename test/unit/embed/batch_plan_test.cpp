// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_all.hpp>
#include <embed/batch_plan.hpp>

#include <algorithm>
#include <cstddef>
#include <numeric>
#include <span>
#include <vector>

namespace
{

using nesso::embed::BATCH_MAX_TEXTS;
using nesso::embed::BATCH_TOKEN_BUDGET;
using nesso::embed::BATCH_WINDOW;
using nesso::embed::BatchPlan;
using nesso::embed::BatchWave;

void requirePermutation(const BatchPlan &plan, size_t count)
{
    std::vector<size_t> seen(count, 0);
    size_t total = 0;
    for (const nesso::embed::PlannedBatch &batch : plan.batches)
    {
        REQUIRE_FALSE(batch.indices.empty());
        CHECK(batch.indices.size() <= BATCH_MAX_TEXTS);
        for (const size_t index : batch.indices)
        {
            REQUIRE(index < count);
            CHECK(seen[index] == 0);
            seen[index] = 1;
            ++total;
        }
    }
    CHECK(total == count);
}

void requireSortedWindows(std::span<const size_t> tokens, const BatchPlan &plan)
{
    const std::vector<size_t> order = nesso::embed::executionOrder(plan);
    REQUIRE(order.size() == tokens.size());
    for (size_t position = 1; position < order.size(); ++position)
    {
        const size_t previous = order[position - 1];
        const size_t current = order[position];
        if (previous / BATCH_WINDOW != current / BATCH_WINDOW)
        {
            continue;
        }
        if (tokens[previous] == tokens[current])
        {
            CHECK(previous < current);
        }
        else
        {
            CHECK(tokens[previous] < tokens[current]);
        }
    }
}

void requireBudget(std::span<const size_t> tokens, const BatchPlan &plan)
{
    for (const nesso::embed::PlannedBatch &batch : plan.batches)
    {
        REQUIRE_FALSE(batch.indices.empty());
        size_t longest = 0;
        for (const size_t index : batch.indices)
        {
            longest = std::max(longest, tokens[index]);
        }
        if (batch.indices.size() > 1)
        {
            CHECK(batch.indices.size() * longest <= BATCH_TOKEN_BUDGET);
        }
    }
}

std::vector<size_t> flatWaves(const std::vector<BatchWave> &waves)
{
    std::vector<size_t> order;
    for (const BatchWave &wave : waves)
    {
        order.insert(order.end(), wave.batchIndices.begin(), wave.batchIndices.end());
    }
    return order;
}

BatchPlan planOf(size_t count, size_t tokens) { return nesso::embed::planBatches(std::vector<size_t>(count, tokens)); }

} // namespace

TEST_CASE("equal token counts keep input order", "[batch-plan]")
{
    const std::vector<size_t> tokens{5, 3, 5, 3, 4};
    const BatchPlan plan = nesso::embed::planBatches(tokens);
    REQUIRE(plan.batches.size() == 1);
    CHECK(plan.batches.front().indices == std::vector<size_t>{1, 3, 4, 0, 2});
    requirePermutation(plan, tokens.size());
    requireSortedWindows(tokens, plan);
}

TEST_CASE("the token budget cuts at every full run", "[batch-plan]")
{
    const auto expectCut = [](size_t tokens, size_t full)
    {
        const BatchPlan exact = planOf(full, tokens);
        REQUIRE(exact.batches.size() == 1);
        CHECK(exact.batches.front().indices.size() == full);

        const BatchPlan overflow = planOf(full + 1, tokens);
        REQUIRE(overflow.batches.size() == 2);
        CHECK(overflow.batches[0].indices.size() == full);
        CHECK(overflow.batches[1].indices.size() == 1);
        CHECK(overflow.batches[1].indices.front() == full);
        requireBudget(std::vector<size_t>(full + 1, tokens), overflow);
    };

    // 32 * 32 = 1024, so the text cap and the token budget close the run together.
    expectCut(32, 32);
    const BatchPlan sixtyFour = planOf(64, 32);
    REQUIRE(sixtyFour.batches.size() == 2);
    CHECK(sixtyFour.batches[0].indices.size() == 32);
    CHECK(sixtyFour.batches[1].indices.size() == 32);
    expectCut(64, 16);
    expectCut(128, 8);
    expectCut(256, 4);

    const BatchPlan lone = planOf(1, 256);
    REQUIRE(lone.batches.size() == 1);
    CHECK(lone.batches.front().indices == std::vector<size_t>{0});
}

TEST_CASE("a text longer than the budget is still one run", "[batch-plan]")
{
    const std::vector<size_t> tokens{BATCH_TOKEN_BUDGET + 1, BATCH_TOKEN_BUDGET + 1};
    const BatchPlan plan = nesso::embed::planBatches(tokens);
    REQUIRE(plan.batches.size() == 2);
    CHECK(plan.batches[0].indices == std::vector<size_t>{0});
    CHECK(plan.batches[1].indices == std::vector<size_t>{1});
}

TEST_CASE("zero-token texts pack until the count cap", "[batch-plan]")
{
    const BatchPlan plan = planOf(BATCH_MAX_TEXTS + 1, 0);
    REQUIRE(plan.batches.size() == 2);
    CHECK(plan.batches[0].indices.size() == BATCH_MAX_TEXTS);
    CHECK(plan.batches[1].indices.size() == 1);
}

TEST_CASE("windows split at 1024 texts", "[batch-plan]")
{
    const auto sizes = [](size_t count)
    {
        const BatchPlan plan = planOf(count, 100);
        std::vector<size_t> out;
        out.reserve(plan.batches.size());
        for (const nesso::embed::PlannedBatch &batch : plan.batches)
        {
            out.push_back(batch.indices.size());
        }
        return out;
    };

    // 10 * 100 = 1000 <= 1024 and 11 * 100 = 1100 does not.
    constexpr size_t full = 10;
    std::vector<size_t> at1023(102, full);
    at1023.push_back(3);
    CHECK(sizes(1023) == at1023);

    std::vector<size_t> at1024(102, full);
    at1024.push_back(4);
    CHECK(sizes(1024) == at1024);

    std::vector<size_t> at1025 = at1024;
    at1025.push_back(1);
    const BatchPlan crossed = planOf(1025, 100);
    CHECK(sizes(1025) == at1025);
    CHECK(crossed.batches.back().indices == std::vector<size_t>{1024});
    for (const size_t index : crossed.batches[crossed.batches.size() - 2].indices)
    {
        CHECK(index < BATCH_WINDOW);
    }
}

TEST_CASE("a short text past the window does not join the previous run", "[batch-plan]")
{
    std::vector<size_t> tokens(BATCH_WINDOW, 200);
    tokens.push_back(1);
    const BatchPlan plan = nesso::embed::planBatches(tokens);
    requirePermutation(plan, tokens.size());
    requireSortedWindows(tokens, plan);
    CHECK(plan.batches.front().indices.size() == 5);
    CHECK(plan.batches.back().indices == std::vector<size_t>{BATCH_WINDOW});
    for (const nesso::embed::PlannedBatch &batch : plan.batches)
    {
        const bool tail = batch.indices.size() == 1 && batch.indices.front() == BATCH_WINDOW;
        if (tail)
        {
            continue;
        }
        for (const size_t index : batch.indices)
        {
            CHECK(index < BATCH_WINDOW);
            CHECK(tokens[index] == 200);
        }
    }
}

TEST_CASE("batch waves do not depend on the session count", "[batch-plan]")
{
    const std::vector<size_t> tokens(130, 32);
    const BatchPlan plan = nesso::embed::planBatches(tokens);
    REQUIRE(plan.batches.size() == 5);

    const std::vector<BatchWave> one = nesso::embed::batchWaves(plan.batches.size(), 1);
    const std::vector<BatchWave> four = nesso::embed::batchWaves(plan.batches.size(), 4);
    const std::vector<size_t> expected{0, 1, 2, 3, 4};
    CHECK(flatWaves(one) == expected);
    CHECK(flatWaves(four) == expected);
    CHECK(one.size() == 5);
    CHECK(four.size() == 2);
    CHECK(four.front().batchIndices.size() == 4);
    CHECK(plan.batches[one.front().batchIndices.front()].indices ==
          plan.batches[four.front().batchIndices.front()].indices);

    const BatchPlan again = nesso::embed::planBatches(tokens);
    REQUIRE(again.batches.size() == plan.batches.size());
    for (size_t index = 0; index < plan.batches.size(); ++index)
    {
        CHECK(again.batches[index].indices == plan.batches[index].indices);
    }

    CHECK(nesso::embed::batchWaves(plan.batches.size(), 0).empty());
    CHECK(nesso::embed::batchWaves(0, 4).empty());
}

TEST_CASE("scatter restores input order", "[batch-plan]")
{
    const std::vector<size_t> tokens{256, 1, 256, 1, 256, 1, 256, 1, 256};
    const BatchPlan plan = nesso::embed::planBatches(tokens);
    REQUIRE(plan.batches.size() == 3);
    CHECK(plan.batches[0].indices == std::vector<size_t>{1, 3, 5, 7});
    CHECK(plan.batches[1].indices == std::vector<size_t>{0, 2, 4, 6});
    CHECK(plan.batches[2].indices == std::vector<size_t>{8});

    std::vector<int> markers(tokens.size());
    std::iota(markers.begin(), markers.end(), 10);
    const std::vector<size_t> order = nesso::embed::executionOrder(plan);
    std::vector<int> planOrder(order.size());
    for (size_t position = 0; position < order.size(); ++position)
    {
        planOrder[position] = markers[order[position]];
    }
    CHECK(nesso::embed::scatter(plan, planOrder) == markers);
    CHECK(nesso::embed::scatter(plan, std::vector<int>{}).empty());
    CHECK(nesso::embed::planBatches(std::span<const size_t>{}).batches.empty());
    CHECK(nesso::embed::executionOrder(BatchPlan{}).empty());
    CHECK(nesso::embed::scatter(BatchPlan{}, std::vector<int>{}).empty());
}

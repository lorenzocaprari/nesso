// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <embed/batch_plan.hpp>

#include <algorithm>
#include <numeric>

namespace nesso::embed
{

// Ascending order makes tokens(next) the padded length of the run that would include it.
[[nodiscard]] static bool fitsBudget(size_t count, size_t tokens)
{
    if (count >= BATCH_MAX_TEXTS)
    {
        return false;
    }
    const size_t next = count + 1;
    if (tokens == 0)
    {
        return true;
    }
    return next <= BATCH_TOKEN_BUDGET / tokens;
}

BatchPlan planBatches(std::span<const size_t> tokenCounts)
{
    BatchPlan plan;
    for (size_t windowStart = 0; windowStart < tokenCounts.size(); windowStart += BATCH_WINDOW)
    {
        const size_t windowEnd = std::min(tokenCounts.size(), windowStart + BATCH_WINDOW);
        std::vector<size_t> order(windowEnd - windowStart);
        std::ranges::iota(order, windowStart);
        std::ranges::stable_sort(order, [&tokenCounts](size_t left, size_t right)
                                 { return tokenCounts[left] < tokenCounts[right]; });

        std::vector<size_t> current;
        current.reserve(BATCH_MAX_TEXTS);
        for (const size_t index : order)
        {
            if (!current.empty() && !fitsBudget(current.size(), tokenCounts[index]))
            {
                plan.batches.push_back(PlannedBatch{.indices = current});
                current.clear();
            }
            current.push_back(index);
        }
        // The window is non-empty, so the loop pushed at least one text.
        plan.batches.push_back(PlannedBatch{.indices = std::move(current)});
    }
    return plan;
}

std::vector<BatchWave> batchWaves(size_t batchCount, size_t sessions)
{
    std::vector<BatchWave> waves;
    if (sessions == 0)
    {
        return waves;
    }
    for (size_t start = 0; start < batchCount; start += sessions)
    {
        const size_t width = std::min(sessions, batchCount - start);
        BatchWave wave;
        wave.batchIndices.reserve(width);
        for (size_t slot = 0; slot < width; ++slot)
        {
            wave.batchIndices.push_back(start + slot);
        }
        waves.push_back(std::move(wave));
    }
    return waves;
}

std::vector<size_t> executionOrder(const BatchPlan &plan)
{
    std::vector<size_t> order;
    for (const PlannedBatch &batch : plan.batches)
    {
        order.insert(order.end(), batch.indices.begin(), batch.indices.end());
    }
    return order;
}

} // namespace nesso::embed

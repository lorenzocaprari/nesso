// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <cstddef>
#include <embed/batch_plan.hpp>

#include <algorithm>
#include <numeric>
#include <span>
#include <utility>
#include <vector>

namespace nesso::embed
{

namespace
{
struct BatchBudget
{
    size_t count = 0;
    size_t tokens = 0;
};
} // namespace

// Ascending order makes tokens(next) the padded length of the run that would
// include it.
[[nodiscard]] static auto FitsBudget(const BatchBudget &budget) -> bool
{
    if (budget.count >= BATCH_MAX_TEXTS)
    {
        return false;
    }
    const size_t next = budget.count + 1;
    if (budget.tokens == 0)
    {
        return true;
    }
    return next <= BATCH_TOKEN_BUDGET / budget.tokens;
}

auto PlanBatches(std::span<const size_t> token_counts) -> BatchPlan
{
    BatchPlan plan;
    for (size_t window_start = 0; window_start < token_counts.size(); window_start += BATCH_WINDOW)
    {
        const size_t window_end = std::min(token_counts.size(), window_start + BATCH_WINDOW);
        std::vector<size_t> order(window_end - window_start);
        std::ranges::iota(order, window_start);
        std::ranges::stable_sort(order, [&token_counts](size_t left, size_t right) -> bool
                                 { return token_counts[left] < token_counts[right]; });

        std::vector<size_t> current;
        current.reserve(BATCH_MAX_TEXTS);
        for (const size_t index : order)
        {
            if (!current.empty() && !FitsBudget({.count = current.size(), .tokens = token_counts[index]}))
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

auto BatchWaves(size_t batch_count, size_t sessions) -> std::vector<BatchWave>
{
    std::vector<BatchWave> waves;
    if (sessions == 0)
    {
        return waves;
    }
    for (size_t start = 0; start < batch_count; start += sessions)
    {
        const size_t width = std::min(sessions, batch_count - start);
        BatchWave wave;
        wave.batch_indices.reserve(width);
        for (size_t slot = 0; slot < width; ++slot)
        {
            wave.batch_indices.push_back(start + slot);
        }
        waves.push_back(std::move(wave));
    }
    return waves;
}

auto ExecutionOrder(const BatchPlan &plan) -> std::vector<size_t>
{
    std::vector<size_t> order;
    for (const PlannedBatch &batch : plan.batches)
    {
        order.insert(order.end(), batch.indices.begin(), batch.indices.end());
    }
    return order;
}

} // namespace nesso::embed

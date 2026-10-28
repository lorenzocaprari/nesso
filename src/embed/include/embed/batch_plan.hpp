// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#ifndef EMBED_BATCH_PLAN_HPP
#define EMBED_BATCH_PLAN_HPP

#include <cstddef>
#include <span>
#include <utility>
#include <vector>

namespace nesso::embed
{

/// Texts in one flush. Not derived from core count, session count, or RAM.
inline constexpr size_t BATCH_WINDOW = 1024;

/// Maximum texts in one padded ONNX run.
inline constexpr size_t BATCH_MAX_TEXTS = 32;

/// Padded-token budget of one ONNX run. The newest text in ascending order sets the length.
inline constexpr size_t BATCH_TOKEN_BUDGET = 1024;

/// Indices into the caller's span, in packed order.
struct PlannedBatch
{
    std::vector<size_t> indices;
};

struct BatchPlan
{
    std::vector<PlannedBatch> batches;
};

/// One wave. `batchIndices[slot]` runs on session `slot`. Membership comes from the plan.
struct BatchWave
{
    std::vector<size_t> batchIndices;
};

/// Split into windows of `BATCH_WINDOW`, sort each by token count then input ordinal, and cut.
/// A text that does not fit starts the next run. A single text is always its own run.
[[nodiscard]] BatchPlan planBatches(std::span<const size_t> tokenCounts);

/// Wave width is `sessions`. Batch order is unchanged. `sessions` of 0 yields no waves.
[[nodiscard]] std::vector<BatchWave> batchWaves(size_t batchCount, size_t sessions);

/// Input index of each embedding in execution order.
[[nodiscard]] std::vector<size_t> executionOrder(const BatchPlan &plan);

/// Move plan-order values back to input order. A size mismatch returns an empty vector.
template <typename T> [[nodiscard]] std::vector<T> scatter(const BatchPlan &plan, std::vector<T> planOrder)
{
    const std::vector<size_t> order = executionOrder(plan);
    if (planOrder.size() != order.size())
    {
        return {};
    }
    std::vector<T> restored(order.size());
    for (size_t position = 0; position < order.size(); ++position)
    {
        restored[order[position]] = std::move(planOrder[position]);
    }
    return restored;
}

} // namespace nesso::embed

#endif // EMBED_BATCH_PLAN_HPP

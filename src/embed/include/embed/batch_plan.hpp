// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#ifndef EMBED_BATCH_PLAN_HPP
#define EMBED_BATCH_PLAN_HPP

#include <cstddef>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace nesso::embed
{

/// Texts in one flush. Not derived from core count, session count, or RAM.
inline constexpr size_t BATCH_WINDOW = 1024;

/// Maximum texts in one planned run. A run is a scheduling group for one
/// session: each of its texts is a separate, unpadded ONNX run.
inline constexpr size_t BATCH_MAX_TEXTS = 32;

/// Token budget of one planned run. The newest text in ascending order counts
/// as the length of every text in the run.
inline constexpr size_t BATCH_TOKEN_BUDGET = 1024;

/// How a planned run reaches ONNX Runtime: every text alone, so a text's vector
/// does not depend on the other texts in its run (#362).
inline constexpr std::string_view BATCH_RUN = "single";

/// Indices into the caller's span, in packed order.
struct PlannedBatch
{
    std::vector<size_t> indices;
};

struct BatchPlan
{
    std::vector<PlannedBatch> batches;
};

/// One wave. `batchIndices[slot]` runs on session `slot`. Membership comes from
/// the plan.
struct BatchWave
{
    std::vector<size_t> batch_indices;
};

/// Split into windows of `BATCH_WINDOW`, sort each by token count then input
/// ordinal, and cut. A text that does not fit starts the next run. A single
/// text is always its own run.
[[nodiscard]] auto PlanBatches(std::span<const size_t> token_counts) -> BatchPlan;

/// Wave width is `sessions`. Batch order is unchanged. `sessions` of 0 yields
/// no waves.
[[nodiscard]] auto BatchWaves(size_t batch_count, size_t sessions) -> std::vector<BatchWave>;

/// Input index of each embedding in execution order.
[[nodiscard]] auto ExecutionOrder(const BatchPlan &plan) -> std::vector<size_t>;

/// Move plan-order values back to input order. A size mismatch returns an empty
/// vector.
template <typename T> [[nodiscard]] auto Scatter(const BatchPlan &plan, std::vector<T> plan_order) -> std::vector<T>
{
    const std::vector<size_t> order = ExecutionOrder(plan);
    if (plan_order.size() != order.size())
    {
        return {};
    }
    std::vector<T> restored(order.size());
    for (size_t position = 0; position < order.size(); ++position)
    {
        restored[order[position]] = std::move(plan_order[position]);
    }
    return restored;
}

} // namespace nesso::embed

#endif // EMBED_BATCH_PLAN_HPP

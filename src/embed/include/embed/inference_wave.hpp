// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#ifndef EMBED_INFERENCE_WAVE_HPP
#define EMBED_INFERENCE_WAVE_HPP

#include <embed/embed_types.hpp>

#include <cstddef>
#include <expected>
#include <functional>
#include <vector>

namespace nesso::embed
{

using SlotResult = std::expected<std::vector<std::vector<float>>, EmbedError>;

/// Runs `width` copies of `work` and joins every thread that started before returning.
///
/// `std::bad_alloc` and `std::system_error` from `work` are stored in that slot as
/// `EmbedError::InferenceFailure`. Any other exception leaves the thread.
/// `std::system_error` while creating a thread (for example `EAGAIN` from `ulimit -u`
/// or a cgroup `pids.max`) is returned as the same error after those threads join.
[[nodiscard]] std::expected<std::vector<SlotResult>, EmbedError>
runInferenceWave(std::size_t width, const std::function<SlotResult(std::size_t)> &work);

} // namespace nesso::embed

#endif // EMBED_INFERENCE_WAVE_HPP

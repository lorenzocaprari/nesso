// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <embed/inference_wave.hpp>

#include <cstddef>
#include <new>
#include <system_error>
#include <thread>

namespace nesso::embed
{

std::expected<std::vector<SlotResult>, EmbedError> runInferenceWave(std::size_t width,
                                                                    const std::function<SlotResult(std::size_t)> &work)
{
    std::vector<SlotResult> slots(width);
    try
    {
        // Declared after `slots`. ~jthread joins before either return, so the
        // `work` reference and `slots` outlive every worker.
        std::vector<std::jthread> workers;
        workers.reserve(width);
        for (std::size_t slot = 0; slot < width; ++slot)
        {
            workers.emplace_back(
                [&work, &slots, slot]
                {
                    try
                    {
                        slots[slot] = work(slot);
                    }
                    // bad_alloc: flat buffers. Ort::Exception stays in infer's try. system_error from work.
                    catch (const std::bad_alloc &)
                    {
                        slots[slot] = std::unexpected(EmbedError::InferenceFailure);
                    }
                    catch (const std::system_error &)
                    {
                        slots[slot] = std::unexpected(EmbedError::InferenceFailure);
                    }
                });
        }
    }
    catch (const std::system_error &)
    {
        // pthread_create failure is an environment limit. InferenceFailure is the
        // existing embed error for this path, so the process does not treat it as a bug.
        return std::unexpected(EmbedError::InferenceFailure);
    }
    return slots;
}

} // namespace nesso::embed

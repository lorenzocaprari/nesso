// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT
#include "include/mach_core/embedder.hpp"
#include "include/mach_core/onnx_embedder.hpp"

#include <cmath>
#include <cstdint>
#include <limits>

namespace mach_core
{
namespace
{

void l2Normalize(std::vector<float> &values)
{
    float sumSq = 0.0f;
    for (float v : values)
    {
        sumSq += v * v;
    }
    if (sumSq == 0.0f)
    {
        return;
    }
    const float inv = 1.0f / std::sqrt(sumSq);
    for (float &v : values)
    {
        v *= inv;
    }
}

uint64_t mix(uint64_t h) noexcept
{
    h ^= h >> 33U;
    h *= 0xff51afd7ed558ccdULL;
    h ^= h >> 33U;
    h *= 0xc4ceb9fe1a85ec53ULL;
    h ^= h >> 33U;
    return h;
}

} // namespace

std::expected<std::vector<float>, EngineError> HashEmbedder::embed(std::string_view text) const
{
    std::vector<float> values(kEmbeddingDims, 0.0f);
    uint64_t h = 14695981039346656037ULL;
    for (const char ch : text)
    {
        const auto c = static_cast<unsigned char>(ch);
        h ^= static_cast<uint64_t>(c);
        h *= 1099511628211ULL;
        const uint64_t mixed = mix(h);
        const size_t idx = static_cast<size_t>(mixed % kEmbeddingDims);
        const auto bits = static_cast<int32_t>(mixed);
        values[idx] += static_cast<float>(bits) / static_cast<float>(std::numeric_limits<int32_t>::max());
    }
    l2Normalize(values);
    return values;
}

std::expected<std::unique_ptr<IEmbedder>, EngineError> makeEmbedder(const std::filesystem::path &modelPath,
                                                                    const std::filesystem::path &vocabPath)
{
    if (modelPath.empty())
    {
        return std::make_unique<HashEmbedder>();
    }

    auto onnx = OnnxEmbedder::create(modelPath, vocabPath);
    if (!onnx)
    {
        return std::unexpected(onnx.error());
    }
    return std::make_unique<OnnxEmbedder>(std::move(*onnx));
}

} // namespace mach_core

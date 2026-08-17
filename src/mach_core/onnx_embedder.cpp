// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT
#include "include/mach_core/onnx_embedder.hpp"

#include <cmath>
#include <onnxruntime_cxx_api.h>
#include <utility>

namespace mach_core
{

struct OnnxEmbedder::Impl
{
    Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "vecgrep"};
    Ort::SessionOptions options;
    std::unique_ptr<Ort::Session> session;
};

OnnxEmbedder::OnnxEmbedder() : m_impl(std::make_unique<Impl>()) {}

OnnxEmbedder::OnnxEmbedder(OnnxEmbedder &&) noexcept = default;
OnnxEmbedder &OnnxEmbedder::operator=(OnnxEmbedder &&) noexcept = default;
OnnxEmbedder::~OnnxEmbedder() = default;

std::expected<OnnxEmbedder, EngineError> OnnxEmbedder::create(const std::filesystem::path &modelPath,
                                                             const std::filesystem::path &vocabPath)
{
    auto tokenizer = Tokenizer::load(vocabPath);
    if (!tokenizer)
    {
        return std::unexpected(tokenizer.error());
    }
    if (!std::filesystem::exists(modelPath))
    {
        return std::unexpected(EngineError::ModelLoadFailure);
    }

    OnnxEmbedder embedder;
    embedder.m_tokenizer = std::move(*tokenizer);
    try
    {
        embedder.m_impl->options.SetIntraOpNumThreads(1);
        embedder.m_impl->options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_BASIC);
        embedder.m_impl->session =
            std::make_unique<Ort::Session>(embedder.m_impl->env, modelPath.c_str(), embedder.m_impl->options);

        Ort::AllocatorWithDefaultOptions allocator;
        const size_t inputCount = embedder.m_impl->session->GetInputCount();
        const size_t outputCount = embedder.m_impl->session->GetOutputCount();
        if (inputCount == 0 || outputCount == 0)
        {
            return std::unexpected(EngineError::ModelLoadFailure);
        }

        embedder.m_inputNames.reserve(inputCount);
        for (size_t i = 0; i < inputCount; ++i)
        {
            auto name = embedder.m_impl->session->GetInputNameAllocated(i, allocator);
            embedder.m_inputNames.emplace_back(name.get());
        }

        embedder.m_outputName = embedder.m_impl->session->GetOutputNameAllocated(0, allocator).get();
        for (size_t i = 0; i < outputCount; ++i)
        {
            auto name = embedder.m_impl->session->GetOutputNameAllocated(i, allocator);
            const std::string candidate = name.get();
            if (candidate.find("sentence") != std::string::npos || candidate.find("embedding") != std::string::npos)
            {
                embedder.m_outputName = candidate;
                break;
            }
        }
    }
    catch (const Ort::Exception &)
    {
        return std::unexpected(EngineError::ModelLoadFailure);
    }
    return embedder;
}

std::expected<std::vector<float>, EngineError> OnnxEmbedder::embed(std::string_view text) const
{
    auto ids = m_tokenizer.encodeIds(text);
    if (!ids)
    {
        return std::unexpected(ids.error());
    }
        auto mask = m_tokenizer.attentionMask(*ids);

    try
    {
        const Ort::MemoryInfo memInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        const std::vector<int64_t> shape{1, static_cast<int64_t>(ids->size())};
        std::vector<int64_t> tokenTypes(ids->size(), 0);

        std::vector<Ort::Value> inputs;
        std::vector<const char *> inputNames;
        inputs.reserve(m_inputNames.size());
        inputNames.reserve(m_inputNames.size());
        for (const auto &name : m_inputNames)
        {
            if (name.find("attention") != std::string::npos)
            {
                inputs.push_back(Ort::Value::CreateTensor<int64_t>(memInfo, mask.data(), mask.size(), shape.data(),
                                                                   shape.size()));
            }
            else if (name.find("token_type") != std::string::npos)
            {
                inputs.push_back(Ort::Value::CreateTensor<int64_t>(memInfo, tokenTypes.data(), tokenTypes.size(),
                                                                   shape.data(), shape.size()));
            }
            else
            {
                inputs.push_back(Ort::Value::CreateTensor<int64_t>(memInfo, ids->data(), ids->size(), shape.data(),
                                                                   shape.size()));
            }
            inputNames.push_back(name.c_str());
        }

        const char *outputNames[1] = {m_outputName.c_str()};
        auto outputs = m_impl->session->Run(Ort::RunOptions{nullptr}, inputNames.data(), inputs.data(), inputs.size(),
                                            outputNames, 1);
        if (outputs.empty() || !outputs.front().IsTensor())
        {
            return std::unexpected(EngineError::InferenceFailure);
        }

        const auto info = outputs.front().GetTensorTypeAndShapeInfo();
        const auto dims = info.GetShape();
        const float *raw = outputs.front().GetTensorData<float>();
        std::vector<float> embedding(kEmbeddingDims, 0.0f);

        if (dims.size() == 2 && dims.back() == static_cast<int64_t>(kEmbeddingDims))
        {
            const auto *begin = raw;
            embedding.assign(begin, begin + static_cast<std::ptrdiff_t>(kEmbeddingDims));
        }
        else if (dims.size() == 3 && dims.back() == static_cast<int64_t>(kEmbeddingDims))
        {
            const auto seq = static_cast<size_t>(dims[1]);
            const auto hidden = static_cast<size_t>(dims[2]);
            float count = 0.0f;
            for (size_t t = 0; t < seq; ++t)
            {
                if (t < mask.size() && mask[t] == 0)
                {
                    continue;
                }
                for (size_t h = 0; h < hidden && h < kEmbeddingDims; ++h)
                {
                    embedding[h] += raw[t * hidden + h];
                }
                count += 1.0f;
            }
            if (count > 0.0f)
            {
                for (float &v : embedding)
                {
                    v /= count;
                }
            }
        }
        else
        {
            return std::unexpected(EngineError::InferenceFailure);
        }

        float sumSq = 0.0f;
        for (float v : embedding)
        {
            sumSq += v * v;
        }
        if (sumSq > 0.0f)
        {
            const float inv = 1.0f / std::sqrt(sumSq);
            for (float &v : embedding)
            {
                v *= inv;
            }
        }
        return embedding;
    }
    catch (const Ort::Exception &)
    {
        return std::unexpected(EngineError::InferenceFailure);
    }
}

} // namespace mach_core

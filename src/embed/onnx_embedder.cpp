// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#include "include/embed/onnx_embedder.hpp"

#include <cmath>
#include <memory>
#include <onnxruntime_cxx_api.h>

namespace embed
{

static std::vector<float> meanPoolAndNormalize(const float *hiddenStates, const int64_t *attentionMask,
                                               size_t sequenceLength, size_t hiddenSize)
{
    std::vector<float> pooled(hiddenSize, 0.0F);
    size_t activeTokens = 0;
    for (size_t token = 0; token < sequenceLength; ++token)
    {
        if (attentionMask[token] == 0)
        {
            continue;
        }
        ++activeTokens;
        const float *tokenVector = hiddenStates + (token * hiddenSize);
        for (size_t dim = 0; dim < hiddenSize; ++dim)
        {
            pooled[dim] += tokenVector[dim];
        }
    }

    if (activeTokens == 0)
    {
        return pooled;
    }

    for (float &value : pooled)
    {
        value /= static_cast<float>(activeTokens);
    }

    float norm = 0.0F;
    for (const float value : pooled)
    {
        norm += value * value;
    }
    norm = std::sqrt(norm);
    if (norm > 0.0F)
    {
        for (float &value : pooled)
        {
            value /= norm;
        }
    }
    return pooled;
}

class OnnxEmbedder::SessionImpl
{
  public:
    explicit SessionImpl(const std::filesystem::path &modelPath);

    [[nodiscard]] Ort::Session &session() noexcept { return session_; }

  private:
    Ort::Env env_{ORT_LOGGING_LEVEL_WARNING, "nesso-embed"};
    Ort::SessionOptions sessionOptions_;
    Ort::Session session_;
};

OnnxEmbedder::SessionImpl::SessionImpl(const std::filesystem::path &modelPath)
    : session_(env_, modelPath.string().c_str(), sessionOptions_)
{
}

OnnxEmbedder::OnnxEmbedder(WordPieceTokenizer tokenizer, const std::filesystem::path &modelPath)
    : session_(std::make_unique<SessionImpl>(modelPath)), tokenizer_(std::move(tokenizer))
{
}

OnnxEmbedder::~OnnxEmbedder() = default;

std::expected<std::unique_ptr<OnnxEmbedder>, EmbedError> OnnxEmbedder::create(const std::filesystem::path &modelDir)
{
    const auto vocabPath = modelDir / "vocab.txt";
    const auto modelPath = modelDir / "model.onnx";
    if (!std::filesystem::exists(vocabPath) || !std::filesystem::exists(modelPath))
    {
        return std::unexpected(EmbedError::ModelLoadFailure);
    }

    auto tokenizer = WordPieceTokenizer::fromVocabFile(vocabPath);
    if (!tokenizer)
    {
        return std::unexpected(tokenizer.error());
    }

    try
    {
        return std::make_unique<OnnxEmbedder>(std::move(*tokenizer), modelPath);
    }
    catch (...)
    {
        return std::unexpected(EmbedError::ModelLoadFailure);
    }
}

std::expected<std::vector<float>, EmbedError> OnnxEmbedder::embed(std::string_view text) const
{
    const std::string owned{text};
    const std::array<std::string, 1> batch{owned};
    const auto batchResult = embedBatch(batch);
    if (!batchResult || batchResult->empty())
    {
        if (!batchResult)
        {
            return std::unexpected(batchResult.error());
        }
        return std::unexpected(EmbedError::InferenceFailure);
    }
    return batchResult->front();
}

std::expected<std::vector<std::vector<float>>, EmbedError>
OnnxEmbedder::embedBatch(std::span<const std::string> texts) const
{
    if (texts.empty())
    {
        return std::unexpected(EmbedError::InvalidInput);
    }

    std::vector<TokenizedInput> encodedBatch;
    encodedBatch.reserve(texts.size());
    for (const std::string &text : texts)
    {
        auto encoded = tokenizer_.encode(text);
        if (!encoded)
        {
            return std::unexpected(encoded.error());
        }
        encodedBatch.push_back(std::move(*encoded));
    }

    const size_t batchSize = encodedBatch.size();
    const size_t maxSequenceLength = tokenizer_.maxSequenceLength();
    std::vector<int64_t> flatInputIds(batchSize * maxSequenceLength, 0);
    std::vector<int64_t> flatAttentionMask(batchSize * maxSequenceLength, 0);
    std::vector<int64_t> flatTokenTypeIds(batchSize * maxSequenceLength, 0);

    for (size_t batchIndex = 0; batchIndex < batchSize; ++batchIndex)
    {
        const TokenizedInput &encoded = encodedBatch[batchIndex];
        for (size_t token = 0; token < encoded.inputIds.size(); ++token)
        {
            const size_t offset = (batchIndex * maxSequenceLength) + token;
            flatInputIds[offset] = encoded.inputIds[token];
            flatAttentionMask[offset] = encoded.attentionMask[token];
            flatTokenTypeIds[offset] = encoded.tokenTypeIds[token];
        }
    }

    try
    {
        const Ort::MemoryInfo memoryInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        const std::array<int64_t, 2> inputShape{static_cast<int64_t>(batchSize),
                                                static_cast<int64_t>(maxSequenceLength)};

        auto inputIdsTensor = Ort::Value::CreateTensor<int64_t>(memoryInfo, flatInputIds.data(), flatInputIds.size(),
                                                                inputShape.data(), inputShape.size());
        auto attentionMaskTensor = Ort::Value::CreateTensor<int64_t>(
            memoryInfo, flatAttentionMask.data(), flatAttentionMask.size(), inputShape.data(), inputShape.size());
        auto tokenTypeIdsTensor = Ort::Value::CreateTensor<int64_t>(
            memoryInfo, flatTokenTypeIds.data(), flatTokenTypeIds.size(), inputShape.data(), inputShape.size());

        const std::array<const char *, 3> inputNames{"input_ids", "attention_mask", "token_type_ids"};
        const std::array<const char *, 1> outputNames{"last_hidden_state"};
        std::array<Ort::Value, 3> inputs{std::move(inputIdsTensor), std::move(attentionMaskTensor),
                                         std::move(tokenTypeIdsTensor)};

        auto outputs = session_->session().Run(Ort::RunOptions{nullptr}, inputNames.data(), inputs.data(),
                                               inputs.size(), outputNames.data(), outputNames.size());
        if (outputs.empty())
        {
            return std::unexpected(EmbedError::InferenceFailure);
        }

        const auto &output = outputs.front();
        const auto *hiddenStates = output.GetTensorData<float>();
        const auto outputInfo = output.GetTensorTypeAndShapeInfo();
        const auto outputShape = outputInfo.GetShape();
        if (outputShape.size() != 3)
        {
            return std::unexpected(EmbedError::InferenceFailure);
        }

        const auto hiddenSize = static_cast<size_t>(outputShape[2]);
        std::vector<std::vector<float>> embeddings;
        embeddings.reserve(batchSize);
        for (size_t batchIndex = 0; batchIndex < batchSize; ++batchIndex)
        {
            const float *batchHidden = hiddenStates + (batchIndex * maxSequenceLength * hiddenSize);
            embeddings.push_back(meanPoolAndNormalize(batchHidden,
                                                      flatAttentionMask.data() + (batchIndex * maxSequenceLength),
                                                      maxSequenceLength, hiddenSize));
        }
        return embeddings;
    }
    catch (...)
    {
        return std::unexpected(EmbedError::InferenceFailure);
    }
}

} // namespace embed

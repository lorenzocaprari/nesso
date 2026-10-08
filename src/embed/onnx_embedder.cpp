// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#include "include/embed/onnx_embedder.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <onnxruntime_cxx_api.h>
#include <stdexcept>
#include <thread>
#include <utility>

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

// One ONNX Run of the whole corpus, padded to 256, asks for tens of gigabytes.
// A batch of 32 padded to its own longest sequence stays under a few hundred
// MB.
static constexpr size_t EMBED_BATCH_SIZE = 32;

struct ModelIo
{
    std::string inputIds;
    std::string attentionMask;
    std::string tokenTypeIds;
    std::string output;
};

static Ort::SessionOptions makeSessionOptions()
{
    Ort::SessionOptions options;
    // Inter-op parallelism finishes graph nodes in an arbitrary order, so two
    // runs of the same input can rank differently. One intra-op pool of fixed
    // size keeps the GEMM reduction order stable.
    options.SetExecutionMode(ORT_SEQUENTIAL);
    options.SetInterOpNumThreads(1);
    const unsigned int threads = std::thread::hardware_concurrency();
    options.SetIntraOpNumThreads(threads == 0 ? 1 : static_cast<int>(threads));
    options.SetGraphOptimizationLevel(ORT_ENABLE_ALL);
    // ORT 1.18 U8S8 quantized GEMM overflows on AVX2 and on AVX-512 without
    // VNNI. The garbage depends on how the GEMM is tiled, which is why the
    // same query scored 0.05 under CPU load on the Xeon. U8U8 does not overflow.
    options.AddConfigEntry("session.x64quantprecision", "1");
    return options;
}

static ModelIo readModelIo(Ort::Session &session)
{
    const Ort::AllocatorWithDefaultOptions allocator;
    ModelIo io;
    for (size_t index = 0; index < session.GetInputCount(); ++index)
    {
        const auto allocated = session.GetInputNameAllocated(index, allocator);
        const std::string name = allocated.get();
        if (name == "input_ids")
        {
            io.inputIds = name;
        }
        else if (name == "attention_mask")
        {
            io.attentionMask = name;
        }
        else if (name == "token_type_ids")
        {
            io.tokenTypeIds = name;
        }
    }
    if (io.inputIds.empty() || io.attentionMask.empty() || io.tokenTypeIds.empty() || session.GetOutputCount() < 1)
    {
        throw std::runtime_error("model is missing input_ids, attention_mask, "
                                 "token_type_ids, or an output");
    }

    const auto outputName = session.GetOutputNameAllocated(0, allocator);
    io.output = outputName.get();

    const auto shape = session.GetOutputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
    if (shape.size() != 3 || std::cmp_not_equal(shape[2], MINILM_EMBEDDING_DIMENSIONS))
    {
        throw std::runtime_error("model hidden size is not 384");
    }
    return io;
}

class OnnxEmbedder::SessionImpl
{
  public:
    explicit SessionImpl(const std::filesystem::path &modelPath);

    [[nodiscard]] std::expected<std::vector<std::vector<float>>, EmbedError>
    infer(std::span<const TokenizedInput> encoded);

  private:
    Ort::Env env_{ORT_LOGGING_LEVEL_WARNING, "nesso-embed"};
    Ort::SessionOptions sessionOptions_;
    Ort::Session session_;
    ModelIo io_;
};

OnnxEmbedder::SessionImpl::SessionImpl(const std::filesystem::path &modelPath)
    : sessionOptions_(makeSessionOptions()), session_(env_, modelPath.string().c_str(), sessionOptions_),
      io_(readModelIo(session_))
{
}

std::expected<std::vector<std::vector<float>>, EmbedError>
OnnxEmbedder::SessionImpl::infer(std::span<const TokenizedInput> encoded)
{
    if (encoded.empty())
    {
        return std::unexpected(EmbedError::InvalidInput);
    }

    size_t paddedLength = 0;
    for (const TokenizedInput &item : encoded)
    {
        paddedLength = std::max(paddedLength, item.inputIds.size());
    }
    if (paddedLength == 0)
    {
        return std::unexpected(EmbedError::InferenceFailure);
    }

    const size_t batchSize = encoded.size();
    std::vector<int64_t> flatInputIds(batchSize * paddedLength, 0);
    std::vector<int64_t> flatAttentionMask(batchSize * paddedLength, 0);
    std::vector<int64_t> flatTokenTypeIds(batchSize * paddedLength, 0);
    for (size_t batchIndex = 0; batchIndex < batchSize; ++batchIndex)
    {
        const TokenizedInput &item = encoded[batchIndex];
        for (size_t token = 0; token < item.inputIds.size(); ++token)
        {
            const size_t offset = (batchIndex * paddedLength) + token;
            flatInputIds[offset] = item.inputIds[token];
            flatAttentionMask[offset] = item.attentionMask[token];
            flatTokenTypeIds[offset] = item.tokenTypeIds[token];
        }
    }

    try
    {
        const Ort::MemoryInfo memoryInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        const std::array<int64_t, 2> inputShape{static_cast<int64_t>(batchSize), static_cast<int64_t>(paddedLength)};
        auto inputIdsTensor = Ort::Value::CreateTensor<int64_t>(memoryInfo, flatInputIds.data(), flatInputIds.size(),
                                                                inputShape.data(), inputShape.size());
        auto attentionMaskTensor = Ort::Value::CreateTensor<int64_t>(
            memoryInfo, flatAttentionMask.data(), flatAttentionMask.size(), inputShape.data(), inputShape.size());
        auto tokenTypeIdsTensor = Ort::Value::CreateTensor<int64_t>(
            memoryInfo, flatTokenTypeIds.data(), flatTokenTypeIds.size(), inputShape.data(), inputShape.size());

        const std::array<const char *, 3> inputNames{io_.inputIds.c_str(), io_.attentionMask.c_str(),
                                                     io_.tokenTypeIds.c_str()};
        const std::array<const char *, 1> outputNames{io_.output.c_str()};
        std::array<Ort::Value, 3> inputs{std::move(inputIdsTensor), std::move(attentionMaskTensor),
                                         std::move(tokenTypeIdsTensor)};

        auto outputs = session_.Run(Ort::RunOptions{nullptr}, inputNames.data(), inputs.data(), inputs.size(),
                                    outputNames.data(), outputNames.size());
        if (outputs.empty())
        {
            return std::unexpected(EmbedError::InferenceFailure);
        }

        const auto &output = outputs.front();
        const auto *hiddenStates = output.GetTensorData<float>();
        const auto outputShape = output.GetTensorTypeAndShapeInfo().GetShape();
        if (outputShape.size() != 3 || std::cmp_not_equal(outputShape[2], MINILM_EMBEDDING_DIMENSIONS))
        {
            return std::unexpected(EmbedError::InferenceFailure);
        }

        std::vector<std::vector<float>> embeddings;
        embeddings.reserve(batchSize);
        for (size_t batchIndex = 0; batchIndex < batchSize; ++batchIndex)
        {
            const float *batchHidden = hiddenStates + (batchIndex * paddedLength * MINILM_EMBEDDING_DIMENSIONS);
            embeddings.push_back(meanPoolAndNormalize(batchHidden,
                                                      flatAttentionMask.data() + (batchIndex * paddedLength),
                                                      paddedLength, MINILM_EMBEDDING_DIMENSIONS));
        }
        return embeddings;
    }
    catch (...)
    {
        return std::unexpected(EmbedError::InferenceFailure);
    }
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

    std::vector<std::vector<float>> embeddings;
    embeddings.reserve(encodedBatch.size());
    for (size_t offset = 0; offset < encodedBatch.size(); offset += EMBED_BATCH_SIZE)
    {
        const size_t count = std::min(EMBED_BATCH_SIZE, encodedBatch.size() - offset);
        const std::span<const TokenizedInput> chunk{encodedBatch.data() + offset, count};
        auto part = session_->infer(chunk);
        if (!part)
        {
            return std::unexpected(part.error());
        }
        embeddings.insert(embeddings.end(), std::make_move_iterator(part->begin()),
                          std::make_move_iterator(part->end()));
    }
    return embeddings;
}

} // namespace embed

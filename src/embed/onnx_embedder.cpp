// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <embed/amx_guard.hpp>
#include <embed/batch_plan.hpp>
#include <embed/inference_wave.hpp>
#include <embed/onnx_embedder.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <format>
#include <fstream>
#include <functional>
#include <memory>
#include <onnxruntime_cxx_api.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace nesso::embed
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

struct ModelIo
{
    std::string inputIds;
    std::string attentionMask;
    std::string tokenTypeIds;
    std::string output;
};

// Debug builds define _GLIBCXX_DEBUG. Copying the model through
// istreambuf_iterator checks every byte, which takes about five seconds under
// ASan for the 22 MiB int8 MiniLM. grep, index, and search each load the model,
// so that copy dominated the Debug BDD suite. The overlap keeps a marker that
// straddles two reads.
static bool modelIsQuantized(const std::filesystem::path &modelPath)
{
    constexpr std::string_view kMatMulInteger = "MatMulInteger";
    constexpr std::string_view kDynamicQuantizeLinear = "DynamicQuantizeLinear";
    constexpr std::size_t kOverlap = kDynamicQuantizeLinear.size() - 1;
    constexpr std::size_t kChunk = std::size_t{64} * std::size_t{1024};

    std::ifstream input(modelPath, std::ios::binary);
    std::array<char, kOverlap + kChunk> buffer{};
    std::size_t kept = 0;
    while (input)
    {
        input.read(buffer.data() + kept, static_cast<std::streamsize>(kChunk));
        const auto got = static_cast<std::size_t>(input.gcount());
        const std::string_view window(buffer.data(), kept + got);
        if (window.find(kMatMulInteger) != std::string_view::npos ||
            window.find(kDynamicQuantizeLinear) != std::string_view::npos)
        {
            return true;
        }
        if (got < kChunk)
        {
            return false;
        }
        std::memmove(buffer.data(), window.data() + window.size() - kOverlap, kOverlap);
        kept = kOverlap;
    }
    return false;
}

static Ort::SessionOptions makeSessionOptions(int intraOpThreads)
{
    Ort::SessionOptions options;
    // Inter-op parallelism finishes graph nodes in an arbitrary order, so two
    // runs of the same input can rank differently. One intra-op pool of fixed
    // size keeps the GEMM reduction order stable.
    options.SetExecutionMode(ORT_SEQUENTIAL);
    options.SetInterOpNumThreads(1);
    options.SetIntraOpNumThreads(intraOpThreads < 1 ? 1 : intraOpThreads);
    options.SetGraphOptimizationLevel(ORT_ENABLE_ALL);
    // ORT 1.18 U8S8 quantized GEMM overflows on AVX2 and on AVX-512 without
    // VNNI. The garbage depends on how the GEMM is tiled, which is why the
    // same query scored 0.05 under CPU load on the Xeon. U8U8 does not overflow.
    // Intra-op threads share that pool. A wider pool stays only when the corpus
    // bytes match a single intra-op thread.
    options.AddConfigEntry("session.x64quantprecision", "1");
    return options;
}

// ORT picks its int8 GEMM kernel once per process, so the AMX refusal goes in
// before the first environment and session. A host where it cannot be
// installed keeps the ORT default.
static Ort::Env makeEnv()
{
    static_cast<void>(denyAmxTiles());
    return Ort::Env{ORT_LOGGING_LEVEL_WARNING, "nesso-embed"};
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
    SessionImpl(const std::filesystem::path &modelPath, size_t requestedThreads);
    SessionImpl(const std::filesystem::path &modelPath, const SessionPolicy &policy);

    [[nodiscard]] const SessionPolicy &policy() const noexcept { return policy_; }

    [[nodiscard]] std::expected<std::vector<std::vector<float>>, EmbedError>
    inferAll(std::span<const TokenizedInput> encoded, const std::function<void(size_t)> &onCompleted);

  private:
    [[nodiscard]] std::expected<std::vector<std::vector<float>>, EmbedError>
    infer(Ort::Session &session, std::span<const TokenizedInput> encoded, std::span<const size_t> indices) const;

    Ort::Env env_ = makeEnv();
    std::vector<std::unique_ptr<Ort::Session>> sessions_;
    ModelIo io_;
    SessionPolicy policy_{};
};

OnnxEmbedder::SessionImpl::SessionImpl(const std::filesystem::path &modelPath, size_t requestedThreads)
    : SessionImpl(modelPath, sessionPolicy(modelIsQuantized(modelPath), requestedThreads, readMachine()))
{
}

OnnxEmbedder::SessionImpl::SessionImpl(const std::filesystem::path &modelPath, const SessionPolicy &policy)
    : policy_(policy)
{
    const Ort::SessionOptions options = makeSessionOptions(policy_.intraOpThreads);
    const std::string path = modelPath.string();
    sessions_.reserve(policy_.sessions);
    for (size_t index = 0; index < policy_.sessions; ++index)
    {
        sessions_.push_back(std::make_unique<Ort::Session>(env_, path.c_str(), options));
    }
    io_ = readModelIo(*sessions_.front());
}

std::expected<std::vector<std::vector<float>>, EmbedError>
OnnxEmbedder::SessionImpl::infer(Ort::Session &session, std::span<const TokenizedInput> encoded,
                                 std::span<const size_t> indices) const
{
    if (indices.empty())
    {
        return std::unexpected(EmbedError::InvalidInput);
    }

    size_t paddedLength = 0;
    for (const size_t index : indices)
    {
        paddedLength = std::max(paddedLength, encoded[index].inputIds.size());
    }
    if (paddedLength == 0)
    {
        return std::unexpected(EmbedError::InferenceFailure);
    }

    const size_t batchSize = indices.size();
    std::vector<int64_t> flatInputIds(batchSize * paddedLength, 0);
    std::vector<int64_t> flatAttentionMask(batchSize * paddedLength, 0);
    std::vector<int64_t> flatTokenTypeIds(batchSize * paddedLength, 0);
    for (size_t batchIndex = 0; batchIndex < batchSize; ++batchIndex)
    {
        const TokenizedInput &item = encoded[indices[batchIndex]];
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

        // Wide int8 sessions and fp32 shrink the CPU arena after this Run. The
        // arena stays enabled: shrinkage is refused when it is disabled. x86
        // single-thread sessions leave the arena, matching a budget of 4 or less.
        // A null run-options pointer is what a budget of 4 or less passes; it
        // cannot take a config entry.
        std::vector<Ort::Value> outputs;
        if (policy_.shrinkArena)
        {
            Ort::RunOptions runOptions;
            runOptions.AddConfigEntry("memory.enable_memory_arena_shrinkage", "cpu:0");
            outputs = session.Run(runOptions, inputNames.data(), inputs.data(), inputs.size(), outputNames.data(),
                                  outputNames.size());
        }
        else
        {
            outputs = session.Run(Ort::RunOptions{nullptr}, inputNames.data(), inputs.data(), inputs.size(),
                                  outputNames.data(), outputNames.size());
        }
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

std::expected<std::vector<std::vector<float>>, EmbedError>
OnnxEmbedder::SessionImpl::inferAll(std::span<const TokenizedInput> encoded,
                                    const std::function<void(size_t)> &onCompleted)
{
    if (encoded.empty())
    {
        return std::unexpected(EmbedError::InvalidInput);
    }

    std::vector<size_t> tokenCounts;
    tokenCounts.reserve(encoded.size());
    for (const TokenizedInput &item : encoded)
    {
        tokenCounts.push_back(item.inputIds.size());
    }
    const BatchPlan plan = planBatches(tokenCounts);
    std::vector<std::expected<std::vector<std::vector<float>>, EmbedError>> parts(plan.batches.size());
    const std::vector<BatchWave> waves = batchWaves(plan.batches.size(), sessions_.size());
    size_t waveStart = 0;
    for (const BatchWave &wave : waves)
    {
        auto waveOutcome =
            runInferenceWave(wave.batchIndices.size(),
                             [this, &wave, encoded, &plan](std::size_t slot)
                             {
                                 const std::size_t batchIndex = wave.batchIndices[slot];
                                 return infer(*sessions_[slot], encoded, plan.batches[batchIndex].indices);
                             });
        if (!waveOutcome)
        {
            return std::unexpected(waveOutcome.error());
        }
        for (std::size_t slot = 0; slot < wave.batchIndices.size(); ++slot)
        {
            parts[wave.batchIndices[slot]] = std::move((*waveOutcome)[slot]);
        }
        if (onCompleted)
        {
            size_t completed = 0;
            for (size_t index = 0; index < waveStart + wave.batchIndices.size(); ++index)
            {
                if (!parts[index])
                {
                    return std::unexpected(parts[index].error());
                }
                completed += parts[index]->size();
                // A wave runs several batches at once. Report each one so
                // progress still advances by a batch.
                if (index >= waveStart)
                {
                    onCompleted(completed);
                }
            }
        }
        waveStart += wave.batchIndices.size();
    }

    std::vector<std::vector<float>> planOrder;
    planOrder.reserve(encoded.size());
    for (const auto &part : parts)
    {
        if (!part)
        {
            return std::unexpected(part.error());
        }
        planOrder.insert(planOrder.end(), part->begin(), part->end());
    }
    return scatter(plan, std::move(planOrder));
}

OnnxEmbedder::OnnxEmbedder(WordPieceTokenizer tokenizer, const std::filesystem::path &modelPath, size_t threads)
    : OnnxEmbedder(std::move(tokenizer), modelPath, sessionPolicy(modelIsQuantized(modelPath), threads, readMachine()))
{
}

OnnxEmbedder::OnnxEmbedder(WordPieceTokenizer tokenizer, const std::filesystem::path &modelPath,
                           const SessionPolicy &policy)
    : session_(std::make_unique<SessionImpl>(modelPath, policy)), tokenizer_(std::move(tokenizer)),
      policy_(session_->policy())
{
}

void OnnxEmbedder::prepareEmbedderEnv()
{
    const Ort::Env env = makeEnv();
    static_cast<void>(env);
}

const SessionPolicy &OnnxEmbedder::policy() const noexcept { return policy_; }

size_t OnnxEmbedder::sessions() const noexcept { return policy_.sessions; }

OnnxEmbedder::~OnnxEmbedder() = default;

std::expected<std::unique_ptr<OnnxEmbedder>, EmbedError> OnnxEmbedder::create(const std::filesystem::path &modelDir,
                                                                              size_t threads)
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
        return std::make_unique<OnnxEmbedder>(std::move(*tokenizer), modelPath, threads);
    }
    catch (...)
    {
        return std::unexpected(EmbedError::ModelLoadFailure);
    }
}

std::expected<std::unique_ptr<OnnxEmbedder>, EmbedError>
OnnxEmbedder::createWithIntraOp(const std::filesystem::path &modelDir, int intraOpThreads)
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

    const int width = std::max(intraOpThreads, intraOpThreadFloor());
    const SessionPolicy policy{.quantized = modelIsQuantized(modelPath),
                               .sessions = 1,
                               .intraOpThreads = width,
                               .memoryCap = 0,
                               .shrinkArena = width > 1};
    try
    {
        return std::unique_ptr<OnnxEmbedder>(new OnnxEmbedder(std::move(*tokenizer), modelPath, policy));
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

std::expected<std::vector<std::vector<float>>, EmbedError> OnnxEmbedder::embedBatch(std::span<const std::string> texts,
                                                                                    ProgressCallback progress) const
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

    return session_->inferAll(encodedBatch,
                              [&progress, total = texts.size()](size_t completed)
                              {
                                  if (progress)
                                  {
                                      progress(completed, total);
                                  }
                              });
}

} // namespace nesso::embed

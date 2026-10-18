// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <embed/onnx_embedder.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <fstream>
#include <functional>
#include <memory>
#include <onnxruntime_cxx_api.h>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <unistd.h>
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

static size_t atLeastOne(size_t count) { return count == 0 ? 1 : count; }

MachineResources thisMachine()
{
    MachineResources machine;
    const unsigned int logical = std::thread::hardware_concurrency();
    machine.logicalThreads = atLeastOne(logical);
    const auto pages = sysconf(_SC_PHYS_PAGES);
    const auto pageSize = sysconf(_SC_PAGESIZE);
    if (pages > 0 && pageSize > 0)
    {
        machine.ramBytes = static_cast<std::uintmax_t>(pages) * static_cast<std::uintmax_t>(pageSize);
    }

    std::set<std::pair<int, int>> cores;
    std::error_code error;
    for (const std::filesystem::directory_entry &entry :
         std::filesystem::directory_iterator("/sys/devices/system/cpu", error))
    {
        const std::string name = entry.path().filename().string();
        if (name.size() < 4 || !name.starts_with("cpu"))
        {
            continue;
        }
        if (!std::ranges::all_of(name.substr(3), [](unsigned char digit) { return digit >= '0' && digit <= '9'; }))
        {
            continue;
        }
        std::ifstream packageFile(entry.path() / "topology/physical_package_id");
        std::ifstream coreFile(entry.path() / "topology/core_id");
        int packageId = 0;
        int coreId = 0;
        if (!(packageFile >> packageId) || !(coreFile >> coreId))
        {
            continue;
        }
        cores.emplace(packageId, coreId);
    }
    machine.physicalCores = cores.empty() ? machine.logicalThreads : cores.size();
    return machine;
}

SessionPolicy sessionPolicy(bool quantized, std::uintmax_t modelBytes, size_t requestedThreads,
                            const MachineResources &machine)
{
    const size_t logical = atLeastOne(machine.logicalThreads);
    if (!quantized)
    {
        const size_t intra = requestedThreads == 0 ? logical : requestedThreads;
        return SessionPolicy{.quantized = false, .sessions = 1, .intraOpThreads = static_cast<int>(intra)};
    }

    size_t sessions = requestedThreads == 0 ? atLeastOne(machine.physicalCores) : requestedThreads;
    // Each int8 session maps the model. Leave half of RAM for the process.
    if (requestedThreads == 0 && modelBytes > 0 && machine.ramBytes > modelBytes)
    {
        const std::uintmax_t fit = (machine.ramBytes / 2) / modelBytes;
        sessions = std::min(sessions, static_cast<size_t>(std::max<std::uintmax_t>(fit, 1)));
    }
    return SessionPolicy{.quantized = true, .sessions = atLeastOne(sessions), .intraOpThreads = 1};
}

std::string sessionPolicyText(size_t requestedThreads, const MachineResources &machine)
{
    const SessionPolicy quantized = sessionPolicy(true, 0, requestedThreads, machine);
    const SessionPolicy floating = sessionPolicy(false, 0, requestedThreads, machine);
    const char *sessionWord = quantized.sessions == 1 ? "session" : "sessions";
    std::string text = std::format("int8: {} single-thread {}", quantized.sessions, sessionWord);
    if (requestedThreads == 0)
    {
        text += ", capped by available RAM";
    }
    text += std::format("\nfp32: {} session, {} intra-op threads", floating.sessions, floating.intraOpThreads);
    return text;
}

static bool modelIsQuantized(const std::filesystem::path &modelPath)
{
    std::ifstream input(modelPath, std::ios::binary);
    const std::string bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    return bytes.find("MatMulInteger") != std::string::npos || bytes.find("DynamicQuantizeLinear") != std::string::npos;
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
    // int8 still races inside one intra-op pool, so those sessions use one thread
    // and a pool of sessions instead.
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
    SessionImpl(const std::filesystem::path &modelPath, size_t requestedThreads);

    [[nodiscard]] const SessionPolicy &policy() const noexcept { return policy_; }

    [[nodiscard]] std::expected<std::vector<std::vector<float>>, EmbedError>
    inferAll(std::span<const TokenizedInput> encoded, const std::function<void(size_t)> &onCompleted);

  private:
    [[nodiscard]] std::expected<std::vector<std::vector<float>>, EmbedError>
    infer(Ort::Session &session, std::span<const TokenizedInput> encoded) const;

    Ort::Env env_{ORT_LOGGING_LEVEL_WARNING, "nesso-embed"};
    std::vector<std::unique_ptr<Ort::Session>> sessions_;
    ModelIo io_;
    SessionPolicy policy_{};
};

OnnxEmbedder::SessionImpl::SessionImpl(const std::filesystem::path &modelPath, size_t requestedThreads)
{
    const bool quantized = modelIsQuantized(modelPath);
    std::uintmax_t modelBytes = 0;
    if (const auto size = std::filesystem::file_size(modelPath); size > 0)
    {
        modelBytes = size;
    }
    policy_ = sessionPolicy(quantized, modelBytes, requestedThreads, thisMachine());
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
OnnxEmbedder::SessionImpl::infer(Ort::Session &session, std::span<const TokenizedInput> encoded) const
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

        auto outputs = session.Run(Ort::RunOptions{nullptr}, inputNames.data(), inputs.data(), inputs.size(),
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

std::expected<std::vector<std::vector<float>>, EmbedError>
OnnxEmbedder::SessionImpl::inferAll(std::span<const TokenizedInput> encoded,
                                    const std::function<void(size_t)> &onCompleted)
{
    if (encoded.empty())
    {
        return std::unexpected(EmbedError::InvalidInput);
    }

    const size_t batchCount = (encoded.size() + EMBED_BATCH_SIZE - 1) / EMBED_BATCH_SIZE;
    std::vector<std::expected<std::vector<std::vector<float>>, EmbedError>> parts(batchCount);
    const size_t pool = sessions_.size();
    for (size_t wave = 0; wave < batchCount; wave += pool)
    {
        const size_t width = std::min(pool, batchCount - wave);
        std::vector<std::thread> workers;
        workers.reserve(width);
        for (size_t slot = 0; slot < width; ++slot)
        {
            const size_t batchIndex = wave + slot;
            workers.emplace_back(
                [this, batchIndex, slot, encoded, &parts]
                {
                    const size_t offset = batchIndex * EMBED_BATCH_SIZE;
                    const size_t count = std::min(EMBED_BATCH_SIZE, encoded.size() - offset);
                    parts[batchIndex] =
                        infer(*sessions_[slot], std::span<const TokenizedInput>{encoded.data() + offset, count});
                });
        }
        for (std::thread &worker : workers)
        {
            worker.join();
        }
        if (onCompleted)
        {
            size_t completed = 0;
            for (size_t index = 0; index < wave + width; ++index)
            {
                if (!parts[index])
                {
                    return std::unexpected(parts[index].error());
                }
                completed += parts[index]->size();
                // A wave runs several batches at once. Report each one so
                // progress still advances by a batch, in input order.
                if (index >= wave)
                {
                    onCompleted(completed);
                }
            }
        }
    }

    std::vector<std::vector<float>> embeddings;
    embeddings.reserve(encoded.size());
    for (const auto &part : parts)
    {
        if (!part)
        {
            return std::unexpected(part.error());
        }
        embeddings.insert(embeddings.end(), part->begin(), part->end());
    }
    return embeddings;
}

OnnxEmbedder::OnnxEmbedder(WordPieceTokenizer tokenizer, const std::filesystem::path &modelPath, size_t threads)
    : session_(std::make_unique<SessionImpl>(modelPath, threads)), tokenizer_(std::move(tokenizer)),
      policy_(session_->policy())
{
}

const SessionPolicy &OnnxEmbedder::policy() const noexcept { return policy_; }

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

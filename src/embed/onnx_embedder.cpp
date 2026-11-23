// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include "embed/embed_types.hpp"
#include "embed/session_policy.hpp"
#include "embed/wordpiece_tokenizer.hpp"
#include <embed/amx_guard.hpp>
#include <embed/batch_plan.hpp>
#include <embed/inference_wave.hpp>
#include <embed/onnx_embedder.hpp>
#include <platform/assert.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <expected>
#include <filesystem>
#include <fstream>
#include <functional>
#include <ios>
#include <memory>
#include <onnxruntime_c_api.h>
#include <onnxruntime_cxx_api.h>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace nesso::embed
{

struct HiddenShape
{
    size_t sequence_length = 0;
    size_t hidden_size = 0;
};

static void AssertEncodedInput(const TokenizedInput &item)
{
    NESSO_ASSERT(item.input_ids.size() == item.attention_mask.size());
    NESSO_ASSERT(item.input_ids.size() == item.token_type_ids.size());
}

static std::vector<float> MeanPoolAndNormalize(std::span<const float> hidden_states,
                                               std::span<const int64_t> attention_mask, const HiddenShape &shape)
{
    NESSO_ASSERT(hidden_states.size() == shape.sequence_length * shape.hidden_size);
    NESSO_ASSERT(attention_mask.size() == shape.sequence_length);
    if (!hidden_states.empty())
    {
        NESSO_ASSERT(hidden_states.data() != nullptr);
    }
    std::vector<float> pooled(shape.hidden_size, 0.0F);
    size_t active_tokens = 0;
    for (size_t token = 0; token < shape.sequence_length; ++token)
    {
        if (attention_mask[token] == 0)
        {
            continue;
        }
        ++active_tokens;
        const std::span<const float> token_vector = hidden_states.subspan(token * shape.hidden_size, shape.hidden_size);
        for (size_t dim = 0; dim < shape.hidden_size; ++dim)
        {
            pooled[dim] += token_vector[dim];
        }
    }

    if (active_tokens == 0)
    {
        return pooled;
    }

    for (float &value : pooled)
    {
        value /= static_cast<float>(active_tokens);
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
    std::string input_ids;
    std::string attention_mask;
    std::string token_type_ids;
    std::string output;
};

// A per-byte istreambuf_iterator copy of the 22 MiB int8 MiniLM dominated the
// Debug BDD suite under ASan. grep, index, and search each load the model.
// The overlap keeps a marker that straddles two reads.
static bool ModelIsQuantized(const std::filesystem::path &model_path)
{
    constexpr std::string_view k_mat_mul_integer = "MatMulInteger";
    constexpr std::string_view k_dynamic_quantize_linear = "DynamicQuantizeLinear";
    constexpr std::size_t k_overlap = k_dynamic_quantize_linear.size() - 1;
    constexpr std::size_t k_chunk = std::size_t{64} * std::size_t{1024};

    std::ifstream input(model_path, std::ios::binary);
    std::array<char, k_overlap + k_chunk> buffer{};
    const std::span<char> storage{buffer};
    std::size_t kept = 0;
    while (input)
    {
        input.read(storage.subspan(kept).data(), static_cast<std::streamsize>(k_chunk));
        const auto got = static_cast<std::size_t>(input.gcount());
        const std::string_view window(storage.data(), kept + got);
        if (window.find(k_mat_mul_integer) != std::string_view::npos ||
            window.find(k_dynamic_quantize_linear) != std::string_view::npos)
        {
            return true;
        }
        if (got < k_chunk)
        {
            return false;
        }
        const std::span<char> overlap = storage.subspan(window.size() - k_overlap, k_overlap);
        std::memmove(storage.data(), overlap.data(), k_overlap);
        kept = k_overlap;
    }
    return false;
}

static Ort::SessionOptions MakeSessionOptions(int intra_op_threads)
{
    Ort::SessionOptions options;
    // Inter-op parallelism finishes graph nodes in an arbitrary order, so two
    // runs of the same input can rank differently. One intra-op pool of fixed
    // size keeps the GEMM reduction order stable.
    options.SetExecutionMode(ORT_SEQUENTIAL);
    options.SetInterOpNumThreads(1);
    options.SetIntraOpNumThreads(intra_op_threads < 1 ? 1 : intra_op_threads);
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
// before the first environment and session. A false result means the filter
// was not installed or tile permission was already granted, so the int8 model
// may use the AMX kernel. That case is one stderr line. A refusal stays quiet.
static Ort::Env MakeEnv()
{
    ReportAmxGuard(DenyAmxTiles(), stderr);
    return Ort::Env{ORT_LOGGING_LEVEL_WARNING, "nesso-embed"};
}

static ModelIo ReadModelIo(Ort::Session &session)
{
    const Ort::AllocatorWithDefaultOptions allocator;
    ModelIo model_io;
    for (size_t index = 0; index < session.GetInputCount(); ++index)
    {
        const auto allocated = session.GetInputNameAllocated(index, allocator);
        const std::string name = allocated.get();
        if (name == "input_ids")
        {
            model_io.input_ids = name;
        }
        else if (name == "attention_mask")
        {
            model_io.attention_mask = name;
        }
        else if (name == "token_type_ids")
        {
            model_io.token_type_ids = name;
        }
    }
    if (model_io.input_ids.empty() || model_io.attention_mask.empty() || model_io.token_type_ids.empty() ||
        session.GetOutputCount() < 1)
    {
        throw std::runtime_error("model is missing input_ids, attention_mask, "
                                 "token_type_ids, or an output");
    }

    const auto output_name = session.GetOutputNameAllocated(0, allocator);
    model_io.output = output_name.get();

    const auto shape = session.GetOutputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
    if (shape.size() != 3 || std::cmp_not_equal(shape[2], MINILM_EMBEDDING_DIMENSIONS))
    {
        throw std::runtime_error("model hidden size is not 384");
    }
    return model_io;
}

class OnnxEmbedder::SessionImpl
{
  public:
    SessionImpl(const std::filesystem::path &model_path, size_t requested_threads);
    SessionImpl(const std::filesystem::path &model_path, const SessionPolicy &policy);

    [[nodiscard]] const SessionPolicy &Policy() const noexcept { return policy_; }

    [[nodiscard]] std::expected<std::vector<std::vector<float>>, EmbedError>
    InferAll(std::span<const TokenizedInput> encoded, const std::function<void(size_t)> &on_completed);

  private:
    [[nodiscard]] std::expected<std::vector<std::vector<float>>, EmbedError>
    Infer(Ort::Session &session, std::span<const TokenizedInput> encoded, std::span<const size_t> indices) const;

    [[nodiscard]] std::expected<std::vector<float>, EmbedError> InferOne(Ort::Session &session,
                                                                         const TokenizedInput &item) const;

    Ort::Env env_ = MakeEnv();
    std::vector<std::unique_ptr<Ort::Session>> sessions_;
    ModelIo io_;
    SessionPolicy policy_{};
};

OnnxEmbedder::SessionImpl::SessionImpl(const std::filesystem::path &model_path, size_t requested_threads)
    : SessionImpl(model_path, MakeSessionPolicy(ModelIsQuantized(model_path), requested_threads, ReadMachine()))
{
}

OnnxEmbedder::SessionImpl::SessionImpl(const std::filesystem::path &model_path, const SessionPolicy &policy)
    : policy_(policy)
{
    const Ort::SessionOptions options = MakeSessionOptions(policy_.intra_op_threads);
    const std::string path = model_path.string();
    sessions_.reserve(policy_.sessions);
    for (size_t index = 0; index < policy_.sessions; ++index)
    {
        sessions_.push_back(std::make_unique<Ort::Session>(env_, path.c_str(), options));
    }
    NESSO_ASSERT(!sessions_.empty());
    io_ = ReadModelIo(*sessions_.front());
}

// A planned run is a scheduling group, not one padded tensor. The int8 model
// quantizes activations with one scale per tensor, so other rows and padded
// positions would move a text's vector (#362). Each text runs alone and
// unpadded, so its vector depends only on its own tokens.
std::expected<std::vector<std::vector<float>>, EmbedError>
OnnxEmbedder::SessionImpl::Infer(Ort::Session &session, std::span<const TokenizedInput> encoded,
                                 std::span<const size_t> indices) const
{
    if (indices.empty())
    {
        return std::unexpected(EmbedError::InvalidInput);
    }
    NESSO_ASSERT(std::ranges::all_of(indices, [&](const size_t index) { return index < encoded.size(); }));

    std::vector<std::vector<float>> embeddings;
    embeddings.reserve(indices.size());
    for (const size_t index : indices)
    {
        auto embedding = InferOne(session, encoded[index]);
        if (!embedding)
        {
            return std::unexpected(embedding.error());
        }
        embeddings.push_back(std::move(*embedding));
    }
    return embeddings;
}

std::expected<std::vector<float>, EmbedError> OnnxEmbedder::SessionImpl::InferOne(Ort::Session &session,
                                                                                  const TokenizedInput &item) const
{
    AssertEncodedInput(item);
    const size_t sequence_length = item.input_ids.size();
    if (sequence_length == 0)
    {
        return std::unexpected(EmbedError::InferenceFailure);
    }
    // The run takes mutable buffers; these copies keep `item` const.
    std::vector<int64_t> input_ids = item.input_ids;
    std::vector<int64_t> attention_mask = item.attention_mask;
    std::vector<int64_t> token_type_ids = item.token_type_ids;

    try
    {
        const Ort::MemoryInfo memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        const std::array<int64_t, 2> input_shape{1, static_cast<int64_t>(sequence_length)};
        auto input_ids_tensor = Ort::Value::CreateTensor<int64_t>(memory_info, input_ids.data(), input_ids.size(),
                                                                  input_shape.data(), input_shape.size());
        auto attention_mask_tensor = Ort::Value::CreateTensor<int64_t>(
            memory_info, attention_mask.data(), attention_mask.size(), input_shape.data(), input_shape.size());
        auto token_type_ids_tensor = Ort::Value::CreateTensor<int64_t>(
            memory_info, token_type_ids.data(), token_type_ids.size(), input_shape.data(), input_shape.size());

        const std::array<const char *, 3> input_names{io_.input_ids.c_str(), io_.attention_mask.c_str(),
                                                      io_.token_type_ids.c_str()};
        const std::array<const char *, 1> output_names{io_.output.c_str()};
        std::array<Ort::Value, 3> inputs{std::move(input_ids_tensor), std::move(attention_mask_tensor),
                                         std::move(token_type_ids_tensor)};

        // Wide int8 sessions and fp32 shrink the CPU arena after this Run. The
        // arena stays enabled: shrinkage is refused when it is disabled. x86
        // single-thread sessions leave the arena, matching a budget of 4 or less.
        // A null run-options pointer is what a budget of 4 or less passes; it
        // cannot take a config entry.
        std::vector<Ort::Value> outputs;
        if (policy_.shrink_arena)
        {
            Ort::RunOptions run_options;
            run_options.AddConfigEntry("memory.enable_memory_arena_shrinkage", "cpu:0");
            outputs = session.Run(run_options, input_names.data(), inputs.data(), inputs.size(), output_names.data(),
                                  output_names.size());
        }
        else
        {
            outputs = session.Run(Ort::RunOptions{nullptr}, input_names.data(), inputs.data(), inputs.size(),
                                  output_names.data(), output_names.size());
        }
        if (outputs.empty())
        {
            return std::unexpected(EmbedError::InferenceFailure);
        }

        const auto &output = outputs.front();
        const auto *hidden_states = output.GetTensorData<float>();
        const auto output_shape = output.GetTensorTypeAndShapeInfo().GetShape();
        if (!MatchesMiniLmHiddenStatesShape(output_shape, 1, sequence_length))
        {
            return std::unexpected(EmbedError::InferenceFailure);
        }
        const std::span<const float> hidden{hidden_states, sequence_length * MINILM_EMBEDDING_DIMENSIONS};
        return MeanPoolAndNormalize(hidden, attention_mask,
                                    {.sequence_length = sequence_length, .hidden_size = MINILM_EMBEDDING_DIMENSIONS});
    }
    catch (...)
    {
        return std::unexpected(EmbedError::InferenceFailure);
    }
}

static std::expected<void, EmbedError> ReportWaveProgress(std::span<const SlotResult> parts, size_t wave_start,
                                                          size_t wave_batches,
                                                          const std::function<void(size_t)> &on_completed)
{
    size_t completed = 0;
    for (size_t index = 0; index < wave_start + wave_batches; ++index)
    {
        if (!parts[index])
        {
            return std::unexpected(parts[index].error());
        }
        completed += parts[index]->size();
        // A wave runs several batches at once. Report each one so
        // progress still advances by a batch.
        if (index >= wave_start)
        {
            on_completed(completed);
        }
    }
    return {};
}

static void StoreWaveParts(const BatchWave &wave, std::vector<SlotResult> &wave_outcome, std::vector<SlotResult> &parts)
{
    NESSO_ASSERT(wave.batch_indices.size() == wave_outcome.size());
    NESSO_ASSERT(
        std::ranges::all_of(wave.batch_indices, [&](const size_t batch_index) { return batch_index < parts.size(); }));
    for (std::size_t slot = 0; slot < wave.batch_indices.size(); ++slot)
    {
        parts[wave.batch_indices[slot]] = std::move(wave_outcome[slot]);
    }
}

std::expected<std::vector<std::vector<float>>, EmbedError>
OnnxEmbedder::SessionImpl::InferAll(std::span<const TokenizedInput> encoded,
                                    const std::function<void(size_t)> &on_completed)
{
    if (encoded.empty())
    {
        return std::unexpected(EmbedError::InvalidInput);
    }

    std::vector<size_t> token_counts;
    token_counts.reserve(encoded.size());
    for (const TokenizedInput &item : encoded)
    {
        token_counts.push_back(item.input_ids.size());
    }
    const BatchPlan plan = PlanBatches(token_counts);
    std::vector<SlotResult> parts(plan.batches.size());
    const std::vector<BatchWave> waves = BatchWaves(plan.batches.size(), sessions_.size());
    size_t wave_start = 0;
    for (const BatchWave &wave : waves)
    {
        auto wave_outcome =
            RunInferenceWave(wave.batch_indices.size(),
                             [this, &wave, encoded, &plan](std::size_t slot)
                             {
                                 NESSO_ASSERT(slot < sessions_.size());
                                 NESSO_ASSERT(slot < wave.batch_indices.size());
                                 const std::size_t batch_index = wave.batch_indices[slot];
                                 NESSO_ASSERT(batch_index < plan.batches.size());
                                 return Infer(*sessions_[slot], encoded, plan.batches[batch_index].indices);
                             });
        if (!wave_outcome)
        {
            return std::unexpected(wave_outcome.error());
        }
        StoreWaveParts(wave, *wave_outcome, parts);
        if (on_completed)
        {
            if (const auto reported = ReportWaveProgress(parts, wave_start, wave.batch_indices.size(), on_completed);
                !reported)
            {
                return std::unexpected(reported.error());
            }
        }
        wave_start += wave.batch_indices.size();
    }

    std::vector<std::vector<float>> plan_order;
    plan_order.reserve(encoded.size());
    for (const auto &part : parts)
    {
        if (!part)
        {
            return std::unexpected(part.error());
        }
        plan_order.insert(plan_order.end(), part->begin(), part->end());
    }
    return Scatter(plan, std::move(plan_order));
}

OnnxEmbedder::OnnxEmbedder(WordPieceTokenizer tokenizer, const std::filesystem::path &model_path, size_t threads)
    : OnnxEmbedder(std::move(tokenizer), model_path,
                   MakeSessionPolicy(ModelIsQuantized(model_path), threads, ReadMachine()))
{
}

OnnxEmbedder::OnnxEmbedder(WordPieceTokenizer tokenizer, const std::filesystem::path &model_path,
                           const SessionPolicy &policy)
    : session_(std::make_unique<SessionImpl>(model_path, policy)), tokenizer_(std::move(tokenizer)),
      policy_(session_->Policy())
{
}

void OnnxEmbedder::PrepareEmbedderEnv()
{
    const Ort::Env env = MakeEnv();
    static_cast<void>(env);
}

const SessionPolicy &OnnxEmbedder::Policy() const noexcept { return policy_; }

size_t OnnxEmbedder::Sessions() const noexcept { return policy_.sessions; }

OnnxEmbedder::~OnnxEmbedder() = default;

std::expected<std::unique_ptr<OnnxEmbedder>, EmbedError> OnnxEmbedder::Create(const std::filesystem::path &model_dir,
                                                                              size_t threads)
{
    const auto vocab_path = model_dir / "vocab.txt";
    const auto model_path = model_dir / "model.onnx";
    if (!std::filesystem::exists(vocab_path) || !std::filesystem::exists(model_path))
    {
        return std::unexpected(EmbedError::ModelLoadFailure);
    }

    auto tokenizer = WordPieceTokenizer::FromVocabFile(vocab_path);
    if (!tokenizer)
    {
        return std::unexpected(tokenizer.error());
    }

    try
    {
        return std::make_unique<OnnxEmbedder>(std::move(*tokenizer), model_path, threads);
    }
    catch (...)
    {
        return std::unexpected(EmbedError::ModelLoadFailure);
    }
}

std::expected<std::unique_ptr<OnnxEmbedder>, EmbedError>
OnnxEmbedder::CreateWithIntraOp(const std::filesystem::path &model_dir, int intra_op_threads)
{
    const auto vocab_path = model_dir / "vocab.txt";
    const auto model_path = model_dir / "model.onnx";
    if (!std::filesystem::exists(vocab_path) || !std::filesystem::exists(model_path))
    {
        return std::unexpected(EmbedError::ModelLoadFailure);
    }

    auto tokenizer = WordPieceTokenizer::FromVocabFile(vocab_path);
    if (!tokenizer)
    {
        return std::unexpected(tokenizer.error());
    }

    const int width = std::max(intra_op_threads, IntraOpThreadFloor());
    const SessionPolicy policy{.quantized = ModelIsQuantized(model_path),
                               .sessions = 1,
                               .intra_op_threads = width,
                               .memory_cap = 0,
                               .shrink_arena = width > 1};
    try
    {
        return std::unique_ptr<OnnxEmbedder>(new OnnxEmbedder(std::move(*tokenizer), model_path, policy));
    }
    catch (...)
    {
        return std::unexpected(EmbedError::ModelLoadFailure);
    }
}

std::expected<std::vector<float>, EmbedError> OnnxEmbedder::Embed(std::string_view text) const
{
    const std::string owned{text};
    const std::array<std::string, 1> batch{owned};
    const auto batch_result = EmbedBatch(batch);
    if (!batch_result || batch_result->empty())
    {
        if (!batch_result)
        {
            return std::unexpected(batch_result.error());
        }
        return std::unexpected(EmbedError::InferenceFailure);
    }
    return batch_result->front();
}

std::expected<std::vector<std::vector<float>>, EmbedError> OnnxEmbedder::EmbedBatch(std::span<const std::string> texts,
                                                                                    ProgressCallback progress) const
{
    if (texts.empty())
    {
        return std::unexpected(EmbedError::InvalidInput);
    }

    std::vector<TokenizedInput> encoded_batch;
    encoded_batch.reserve(texts.size());
    for (const std::string &text : texts)
    {
        auto encoded = tokenizer_.Encode(text);
        if (!encoded)
        {
            return std::unexpected(encoded.error());
        }
        encoded_batch.push_back(std::move(*encoded));
    }

    return session_->InferAll(encoded_batch,
                              [&progress, total = texts.size()](size_t completed)
                              {
                                  if (progress)
                                  {
                                      progress(completed, total);
                                  }
                              });
}

} // namespace nesso::embed

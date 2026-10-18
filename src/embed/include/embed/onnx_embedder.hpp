// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#ifndef EMBED_ONNX_EMBEDDER_HPP
#define EMBED_ONNX_EMBEDDER_HPP

#include <embed/embed_types.hpp>
#include <embed/wordpiece_tokenizer.hpp>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace nesso::embed
{

struct MachineResources
{
    size_t physicalCores = 1;
    size_t logicalThreads = 1;
    std::uintmax_t ramBytes = 0;
};

struct SessionPolicy
{
    bool quantized = false;
    size_t sessions = 1;
    int intraOpThreads = 1;
};

/// `requestedThreads` of 0 selects the machine default. An explicit count overrides it.
[[nodiscard]] SessionPolicy sessionPolicy(bool quantized, std::uintmax_t modelBytes, size_t requestedThreads,
                                          const MachineResources &machine);

[[nodiscard]] std::string sessionPolicyText(size_t requestedThreads, const MachineResources &machine);

[[nodiscard]] MachineResources thisMachine();

class OnnxEmbedder
{
  public:
    using ProgressCallback = std::function<void(size_t completed, size_t total)>;

    [[nodiscard]] static std::expected<std::unique_ptr<OnnxEmbedder>, EmbedError>
    create(const std::filesystem::path &modelDir, size_t threads = 0);

    OnnxEmbedder(WordPieceTokenizer tokenizer, const std::filesystem::path &modelPath, size_t threads);

    [[nodiscard]] const SessionPolicy &policy() const noexcept;

    [[nodiscard]] std::expected<std::vector<float>, EmbedError> embed(std::string_view text) const;

    [[nodiscard]] std::expected<std::vector<std::vector<float>>, EmbedError>
    embedBatch(std::span<const std::string> texts, ProgressCallback progress = {}) const;

    OnnxEmbedder(const OnnxEmbedder &) = delete;
    OnnxEmbedder &operator=(const OnnxEmbedder &) = delete;
    OnnxEmbedder(OnnxEmbedder &&) = delete;
    OnnxEmbedder &operator=(OnnxEmbedder &&) = delete;
    ~OnnxEmbedder();

  private:
    struct SessionImpl;
    std::unique_ptr<SessionImpl> session_;
    WordPieceTokenizer tokenizer_;
    SessionPolicy policy_{};
};

} // namespace nesso::embed

#endif // EMBED_ONNX_EMBEDDER_HPP

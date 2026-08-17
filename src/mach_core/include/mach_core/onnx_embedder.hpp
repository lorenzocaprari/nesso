// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT
#ifndef MACH_CORE_ONNX_EMBEDDER_HPP
#define MACH_CORE_ONNX_EMBEDDER_HPP

#include "core_types.hpp"
#include "embedder.hpp"
#include "tokenizer.hpp"

#include <expected>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace mach_core
{

class OnnxEmbedder final : public IEmbedder
{
  public:
    [[nodiscard]] static std::expected<OnnxEmbedder, EngineError> create(const std::filesystem::path &modelPath,
                                                                         const std::filesystem::path &vocabPath);

    OnnxEmbedder(OnnxEmbedder &&) noexcept;
    OnnxEmbedder &operator=(OnnxEmbedder &&) noexcept;
    ~OnnxEmbedder() override;

    OnnxEmbedder(const OnnxEmbedder &) = delete;
    OnnxEmbedder &operator=(const OnnxEmbedder &) = delete;

    [[nodiscard]] std::expected<std::vector<float>, EngineError> embed(std::string_view text) const override;

  private:
    OnnxEmbedder();

    struct Impl;
    std::unique_ptr<Impl> m_impl;
    Tokenizer m_tokenizer;
    std::vector<std::string> m_inputNames;
    std::string m_outputName;
};

} // namespace mach_core

#endif // MACH_CORE_ONNX_EMBEDDER_HPP

// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#ifndef EMBED_ORT_SESSION_HPP
#define EMBED_ORT_SESSION_HPP

#include <filesystem>
#include <memory>
#include <onnxruntime_cxx_api.h>

namespace nesso::embed
{

/// Builds an ORT session for `model_path` using the platform path encoding.
[[nodiscard]] auto MakeOrtSession(const Ort::Env &env, const std::filesystem::path &model_path,
                                  const Ort::SessionOptions &options) -> std::unique_ptr<Ort::Session>;

} // namespace nesso::embed

#endif // EMBED_ORT_SESSION_HPP

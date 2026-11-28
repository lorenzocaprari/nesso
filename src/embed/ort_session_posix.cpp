// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <embed/ort_session.hpp>

#include <filesystem>
#include <memory>
#include <onnxruntime_cxx_api.h>
#include <string>

namespace nesso::embed
{

auto MakeOrtSession(const Ort::Env &env, const std::filesystem::path &model_path, const Ort::SessionOptions &options)
    -> std::unique_ptr<Ort::Session>
{
    const std::string path = model_path.string();
    return std::make_unique<Ort::Session>(env, path.c_str(), options);
}

} // namespace nesso::embed

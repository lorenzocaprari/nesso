// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#ifndef NESSO_MODEL_PATHS_HPP
#define NESSO_MODEL_PATHS_HPP

#include <filesystem>
#include <vector>

namespace nesso
{

inline constexpr const char *VOCAB_FILENAME = "vocab.txt";
inline constexpr const char *MODEL_FILENAME = "model.onnx";

/// Roots consulted when resolving the default MiniLM directory.
struct ModelDirLookup
{
    std::filesystem::path cwd{std::filesystem::current_path()};
    std::filesystem::path packagedDir{"/usr/share/nesso"};
};

/// True when dir contains both vocab.txt and model.onnx.
[[nodiscard]] bool containsModelFiles(const std::filesystem::path &dir);

/// Directories consulted when --model-dir is omitted, in search order. A set NESSO_MODEL_DIR is the only entry,
/// because that path is used even when it is incomplete.
[[nodiscard]] std::vector<std::filesystem::path> modelDirCandidates(const ModelDirLookup &lookup = {});

/// Default MiniLM directory when --model-dir is omitted:
/// NESSO_MODEL_DIR, cwd/models (if complete), $XDG_DATA_HOME/nesso, then packagedDir.
[[nodiscard]] std::filesystem::path resolveDefaultModelDir(const ModelDirLookup &lookup = {});

} // namespace nesso

#endif // NESSO_MODEL_PATHS_HPP

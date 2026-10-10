// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#ifndef NESSO_MODEL_PATHS_HPP
#define NESSO_MODEL_PATHS_HPP

#include <filesystem>
#include <vector>

namespace nesso
{

inline constexpr const char *VOCAB_FILENAME = "vocab.txt";
inline constexpr const char *MODEL_FILENAME = "model.onnx";

/// True when dir contains both vocab.txt and model.onnx.
[[nodiscard]] bool containsModelFiles(const std::filesystem::path &dir);

/// Directories consulted when --model-dir is omitted, in search order. A set NESSO_MODEL_DIR is the only entry,
/// because that path is used even when it is incomplete.
[[nodiscard]] std::vector<std::filesystem::path>
modelDirCandidates(const std::filesystem::path &cwd = std::filesystem::current_path(),
                   const std::filesystem::path &packagedDir = "/usr/share/nesso");

/// Default MiniLM directory when --model-dir is omitted:
/// NESSO_MODEL_DIR, cwd/models (if complete), $XDG_DATA_HOME/nesso, then packagedDir.
[[nodiscard]] std::filesystem::path
resolveDefaultModelDir(const std::filesystem::path &cwd = std::filesystem::current_path(),
                       const std::filesystem::path &packagedDir = "/usr/share/nesso");

} // namespace nesso

#endif // NESSO_MODEL_PATHS_HPP

// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#ifndef NESSO_MODEL_PATH_ROOTS_HPP
#define NESSO_MODEL_PATH_ROOTS_HPP

#include <filesystem>
#include <vector>

namespace nesso
{

/// Packaged MiniLM directory for this platform (e.g. /usr/share/nesso).
[[nodiscard]] auto DefaultPackagedModelDir() -> std::filesystem::path;

/// Extra user data directories after cwd/models, before the packaged dir.
[[nodiscard]] auto UserModelDataDirs() -> std::vector<std::filesystem::path>;

} // namespace nesso

#endif // NESSO_MODEL_PATH_ROOTS_HPP

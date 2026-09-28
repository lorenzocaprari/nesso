// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#include "model_paths.hpp"

#include <cstdlib>

namespace nesso
{

// getenv is not thread-safe. The CLI reads the environment once on the main thread.
static const char *envOrEmpty(const char *key)
{
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    const char *value = std::getenv(key);
    return value == nullptr ? "" : value;
}

bool containsModelFiles(const std::filesystem::path &dir)
{
    std::error_code ec;
    return std::filesystem::is_regular_file(dir / VOCAB_FILENAME, ec) &&
           std::filesystem::is_regular_file(dir / MODEL_FILENAME, ec);
}

std::filesystem::path resolveDefaultModelDir(const std::filesystem::path &cwd, const std::filesystem::path &packagedDir)
{
    if (const char *envDir = envOrEmpty("NESSO_MODEL_DIR"); envDir[0] != '\0')
    {
        return envDir;
    }

    std::filesystem::path cwdModels = cwd / "models";
    if (containsModelFiles(cwdModels))
    {
        return cwdModels;
    }

    std::filesystem::path xdgNesso;
    if (const char *xdg = envOrEmpty("XDG_DATA_HOME"); xdg[0] != '\0')
    {
        xdgNesso = std::filesystem::path(xdg) / "nesso";
    }
    else if (const char *home = envOrEmpty("HOME"); home[0] != '\0')
    {
        xdgNesso = std::filesystem::path(home) / ".local" / "share" / "nesso";
    }
    if (!xdgNesso.empty() && containsModelFiles(xdgNesso))
    {
        return xdgNesso;
    }

    return packagedDir;
}

} // namespace nesso

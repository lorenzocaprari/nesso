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

std::vector<std::filesystem::path> modelDirCandidates(const std::filesystem::path &cwd,
                                                      const std::filesystem::path &packagedDir)
{
    if (const char *envDir = envOrEmpty("NESSO_MODEL_DIR"); envDir[0] != '\0')
    {
        return {envDir};
    }

    std::vector<std::filesystem::path> dirs;
    dirs.push_back(cwd / "models");
    if (const char *xdg = envOrEmpty("XDG_DATA_HOME"); xdg[0] != '\0')
    {
        dirs.push_back(std::filesystem::path(xdg) / "nesso");
    }
    else if (const char *home = envOrEmpty("HOME"); home[0] != '\0')
    {
        dirs.push_back(std::filesystem::path(home) / ".local" / "share" / "nesso");
    }
    dirs.push_back(packagedDir);
    return dirs;
}

std::filesystem::path resolveDefaultModelDir(const std::filesystem::path &cwd, const std::filesystem::path &packagedDir)
{
    if (const char *envDir = envOrEmpty("NESSO_MODEL_DIR"); envDir[0] != '\0')
    {
        return envDir;
    }

    for (const std::filesystem::path &dir : modelDirCandidates(cwd, packagedDir))
    {
        if (containsModelFiles(dir))
        {
            return dir;
        }
    }
    return packagedDir;
}

} // namespace nesso

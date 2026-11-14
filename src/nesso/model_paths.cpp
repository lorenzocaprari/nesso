// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <nesso/model_paths.hpp>

#include <cstdlib>
#include <string_view>

namespace nesso
{

// getenv is not thread-safe. The CLI reads the environment once on the main
// thread.
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

std::vector<std::filesystem::path> modelDirCandidates(const ModelDirLookup &lookup)
{
    if (const char *envDir = envOrEmpty("NESSO_MODEL_DIR"); !std::string_view{envDir}.empty())
    {
        return {envDir};
    }

    std::vector<std::filesystem::path> dirs;
    dirs.push_back(lookup.cwd / "models");
    if (const char *xdg = envOrEmpty("XDG_DATA_HOME"); !std::string_view{xdg}.empty())
    {
        dirs.push_back(std::filesystem::path(xdg) / "nesso");
    }
    else if (const char *home = envOrEmpty("HOME"); !std::string_view{home}.empty())
    {
        dirs.push_back(std::filesystem::path(home) / ".local" / "share" / "nesso");
    }
    dirs.push_back(lookup.packagedDir);
    return dirs;
}

std::filesystem::path resolveDefaultModelDir(const ModelDirLookup &lookup)
{
    if (const char *envDir = envOrEmpty("NESSO_MODEL_DIR"); !std::string_view{envDir}.empty())
    {
        return envDir;
    }

    for (const std::filesystem::path &dir : modelDirCandidates(lookup))
    {
        if (containsModelFiles(dir))
        {
            return dir;
        }
    }
    return lookup.packagedDir;
}

} // namespace nesso

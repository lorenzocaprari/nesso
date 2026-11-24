// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <filesystem>
#include <nesso/model_paths.hpp>

#include <cstdlib>
#include <string_view>
#include <system_error>
#include <vector>

namespace nesso
{

// getenv is not thread-safe. The CLI reads the environment once on the main
// thread.
static auto EnvOrEmpty(const char *key) -> const char *
{
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    const char *value = std::getenv(key);
    return value == nullptr ? "" : value;
}

auto ContainsModelFiles(const std::filesystem::path &dir) -> bool
{
    std::error_code error_code;
    return std::filesystem::is_regular_file(dir / VOCAB_FILENAME, error_code) &&
           std::filesystem::is_regular_file(dir / MODEL_FILENAME, error_code);
}

auto ModelDirCandidates(const ModelDirLookup &lookup) -> std::vector<std::filesystem::path>
{
    if (const char *env_dir = EnvOrEmpty("NESSO_MODEL_DIR"); !std::string_view{env_dir}.empty())
    {
        return {env_dir};
    }

    std::vector<std::filesystem::path> dirs;
    dirs.push_back(lookup.cwd / "models");
    if (const char *xdg = EnvOrEmpty("XDG_DATA_HOME"); !std::string_view{xdg}.empty())
    {
        dirs.push_back(std::filesystem::path(xdg) / "nesso");
    }
    else if (const char *home = EnvOrEmpty("HOME"); !std::string_view{home}.empty())
    {
        dirs.push_back(std::filesystem::path(home) / ".local" / "share" / "nesso");
    }
    dirs.push_back(lookup.packaged_dir);
    return dirs;
}

auto ResolveDefaultModelDir(const ModelDirLookup &lookup) -> std::filesystem::path
{
    if (const char *env_dir = EnvOrEmpty("NESSO_MODEL_DIR"); !std::string_view{env_dir}.empty())
    {
        return env_dir;
    }

    for (const std::filesystem::path &dir : ModelDirCandidates(lookup))
    {
        if (ContainsModelFiles(dir))
        {
            return dir;
        }
    }
    return lookup.packaged_dir;
}

} // namespace nesso

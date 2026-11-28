// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <nesso/model_path_roots.hpp>

#include <cstdlib>
#include <filesystem>
#include <string_view>
#include <vector>

namespace nesso
{

static auto EnvOrEmpty(const char *key) -> const char *
{
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    const char *value = std::getenv(key);
    return value == nullptr ? "" : value;
}

auto DefaultPackagedModelDir() -> std::filesystem::path { return "/usr/share/nesso"; }

auto UserModelDataDirs() -> std::vector<std::filesystem::path>
{
    std::vector<std::filesystem::path> dirs;
    if (const char *xdg = EnvOrEmpty("XDG_DATA_HOME"); !std::string_view{xdg}.empty())
    {
        dirs.push_back(std::filesystem::path(xdg) / "nesso");
    }
    else if (const char *home = EnvOrEmpty("HOME"); !std::string_view{home}.empty())
    {
        dirs.push_back(std::filesystem::path(home) / ".local" / "share" / "nesso");
    }
    return dirs;
}

} // namespace nesso

// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <nesso/model_path_roots.hpp>

#include <cstdlib>
#include <filesystem>
#include <string_view>
#include <vector>
#include <windows.h>

namespace nesso
{

static auto EnvOrEmpty(const char *key) -> const char *
{
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    const char *value = std::getenv(key);
    return value == nullptr ? "" : value;
}

auto DefaultPackagedModelDir() -> std::filesystem::path
{
    wchar_t module_path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, module_path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH)
    {
        return {};
    }
    return std::filesystem::path(module_path).parent_path() / "models";
}

auto UserModelDataDirs() -> std::vector<std::filesystem::path>
{
    std::vector<std::filesystem::path> dirs;
    if (const char *local = EnvOrEmpty("LOCALAPPDATA"); !std::string_view{local}.empty())
    {
        dirs.push_back(std::filesystem::path(local) / "nesso");
    }
    return dirs;
}

} // namespace nesso

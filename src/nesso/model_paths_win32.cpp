// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <nesso/model_path_roots.hpp>

#include <filesystem>
#include <string>
#include <vector>
#include <windows.h>

namespace nesso
{

auto DefaultPackagedModelDir() -> std::filesystem::path
{
    std::wstring module_path(MAX_PATH, L'\0');
    for (;;)
    {
        const DWORD length = GetModuleFileNameW(nullptr, module_path.data(), static_cast<DWORD>(module_path.size()));
        if (length == 0)
        {
            return {};
        }
        if (length < module_path.size())
        {
            module_path.resize(length);
            return std::filesystem::path(module_path).parent_path() / "models";
        }
        module_path.resize(module_path.size() * 2);
    }
}

auto UserModelDataDirs() -> std::vector<std::filesystem::path>
{
    std::vector<std::filesystem::path> dirs;
    const DWORD needed = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
    if (needed == 0)
    {
        return dirs;
    }
    std::wstring local(needed, L'\0');
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", local.data(), needed);
    if (length == 0 || length >= needed)
    {
        return dirs;
    }
    local.resize(length);
    dirs.push_back(std::filesystem::path(local) / "nesso");
    return dirs;
}

} // namespace nesso

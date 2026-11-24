// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <embed/session_policy.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <windows.h>

namespace nesso::embed
{

static auto AtLeastOne(size_t count) -> size_t { return count == 0 ? 1 : count; }

auto ReadMachine(const MachineFsRoots & /*roots*/) -> MachineResources
{
    MachineResources machine;
    machine.logical_threads = AtLeastOne(std::thread::hardware_concurrency());

    SYSTEM_INFO system_info{};
    GetSystemInfo(&system_info);
    machine.physical_cores = AtLeastOne(static_cast<size_t>(system_info.dwNumberOfProcessors));

    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    if (GlobalMemoryStatusEx(&status) != 0)
    {
        machine.memory_limit_bytes = static_cast<std::uintmax_t>(status.ullAvailPhys);
    }
    return machine;
}

} // namespace nesso::embed

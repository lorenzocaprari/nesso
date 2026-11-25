// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <embed/session_policy.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <thread>
#include <windows.h>

namespace nesso::embed
{

static auto AtLeastOne(size_t count) -> size_t { return count == 0 ? 1 : count; }

static auto PhysicalCoreCount() -> size_t
{
    DWORD bytes = 0;
    GetLogicalProcessorInformation(nullptr, &bytes);
    if (bytes == 0)
    {
        return AtLeastOne(std::thread::hardware_concurrency());
    }
    const size_t count = bytes / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
    auto buffer = std::make_unique<SYSTEM_LOGICAL_PROCESSOR_INFORMATION[]>(count);
    if (GetLogicalProcessorInformation(buffer.get(), &bytes) == 0)
    {
        return AtLeastOne(std::thread::hardware_concurrency());
    }
    size_t cores = 0;
    for (size_t index = 0; index < count; ++index)
    {
        if (buffer[index].Relationship == RelationProcessorCore)
        {
            ++cores;
        }
    }
    return AtLeastOne(cores);
}

auto ReadMachine(const MachineFsRoots & /*roots*/) -> MachineResources
{
    MachineResources machine;
    machine.logical_threads = AtLeastOne(std::thread::hardware_concurrency());
    machine.physical_cores = PhysicalCoreCount();

    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    if (GlobalMemoryStatusEx(&status) != 0)
    {
        // Cap by both reusable RAM and remaining commit. A successful call that
        // reports 0 must not look like "unknown" (also 0); use 1 byte.
        const auto avail = (std::min)(status.ullAvailPhys, status.ullAvailPageFile);
        machine.memory_limit_bytes = avail == 0 ? 1 : static_cast<std::uintmax_t>(avail);
    }
    return machine;
}

} // namespace nesso::embed

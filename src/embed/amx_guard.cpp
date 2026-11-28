// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <embed/amx_guard.hpp>

#include <cstdio>
#include <print>

namespace nesso::embed
{

void ReportAmxGuard(bool guard_in_effect, std::FILE *err)
{
    if (guard_in_effect)
    {
        return;
    }
    std::println(err, "Warning: the AMX guard is not in effect; the int8 model "
                      "may use the AMX kernel");
}

} // namespace nesso::embed

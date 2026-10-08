// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#include "trace.hpp"

#include <platform/file.hpp>

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace nesso
{
namespace
{

struct Stage
{
    std::string name;
    std::int64_t microseconds = 0;
};

struct TraceState
{
    int depth = 0;
    std::vector<Stage> stages;
};

} // namespace

static TraceState &state()
{
    static TraceState current;
    return current;
}

// getenv is not thread-safe. The CLI reads it on the main thread, once per stage.
static bool tracingEnabled()
{
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    const char *value = std::getenv("NESSO_TRACE");
    return value != nullptr && value[0] == '1' && value[1] == '\0';
}

static void emit(TraceState &current)
{
    if (current.stages.empty())
    {
        return;
    }

    const std::int64_t rssBytes = platform::peakResidentBytes();

    std::cerr << R"({"peakRssBytes":)" << rssBytes << R"(,"stages":[)";
    for (size_t index = 0; index < current.stages.size(); ++index)
    {
        if (index != 0)
        {
            std::cerr << ',';
        }
        const Stage &stage = current.stages[index];
        std::cerr << R"({"name":")" << stage.name << R"(","us":)" << stage.microseconds << '}';
    }
    std::cerr << "]}\n";
    current.stages.clear();
}

TraceSession::TraceSession() : active_(tracingEnabled())
{
    if (active_)
    {
        ++state().depth;
    }
}

TraceSession::~TraceSession()
{
    if (!active_)
    {
        return;
    }
    TraceState &current = state();
    --current.depth;
    if (current.depth == 0)
    {
        emit(current);
    }
}

ScopedStage::ScopedStage(std::string_view name)
    : active_(tracingEnabled()), name_(name), start_(std::chrono::steady_clock::now())
{
}

ScopedStage::~ScopedStage()
{
    if (!active_)
    {
        return;
    }
    const auto elapsed = std::chrono::steady_clock::now() - start_;
    const auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
    state().stages.push_back(Stage{.name = std::move(name_), .microseconds = microseconds});
}

} // namespace nesso

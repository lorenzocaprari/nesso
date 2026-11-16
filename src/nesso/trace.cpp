// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <chrono>
#include <nesso/trace.hpp>

#include <platform/file.hpp>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>

namespace nesso
{
namespace
{

struct Stage
{
    std::string name;
    std::int64_t microseconds = 0;
};

constexpr std::size_t MAX_STAGES = 16;

struct TraceState
{
    int depth = 0;
    std::array<Stage, MAX_STAGES> stages{};
    std::size_t stage_count = 0;
};

} // namespace

static TraceState &State()
{
    static TraceState current;
    return current;
}

// getenv is not thread-safe. The CLI reads it on the main thread, once per
// stage.
static bool TracingEnabled()
{
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    const char *value = std::getenv("NESSO_TRACE");
    return value != nullptr && std::string_view{value} == "1";
}

static void Emit(TraceState &current)
{
    if (current.stage_count == 0)
    {
        return;
    }

    const std::int64_t rss_bytes = nesso::platform::PeakResidentBytes();

    std::cerr << R"({"peakRssBytes":)" << rss_bytes << R"(,"stages":[)";
    for (size_t index = 0; index < current.stage_count; ++index)
    {
        if (index != 0)
        {
            std::cerr << ',';
        }
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
        const Stage &stage = current.stages[index];
        std::cerr << R"({"name":")" << stage.name << R"(","us":)" << stage.microseconds << '}';
    }
    std::cerr << "]}\n";
    current.stage_count = 0;
}

TraceSession::TraceSession() : active_(TracingEnabled())
{
    if (active_)
    {
        ++State().depth;
    }
}

TraceSession::~TraceSession()
{
    if (!active_)
    {
        return;
    }
    TraceState &current = State();
    --current.depth;
    if (current.depth == 0)
    {
        Emit(current);
    }
}

ScopedStage::ScopedStage(std::string_view name)
    : active_(TracingEnabled()), name_(name), start_(std::chrono::steady_clock::now())
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
    TraceState &current = State();
    if (current.stage_count >= current.stages.size())
    {
        return;
    }
    // Bounds checked above; operator[] stays non-throwing for the noexcept dtor.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
    Stage &slot = current.stages[current.stage_count];
    slot.name = std::move(name_);
    slot.microseconds = microseconds;
    ++current.stage_count;
}

} // namespace nesso

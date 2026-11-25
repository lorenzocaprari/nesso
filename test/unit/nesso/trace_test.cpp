// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_all.hpp>
#include <nesso/trace.hpp>

#include "../env_set.hpp"

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>

namespace
{

struct RestoreCerr
{
    std::streambuf *original = std::cerr.rdbuf();
    ~RestoreCerr() { std::cerr.rdbuf(original); }
};

struct ClearTraceEnv
{
    ~ClearTraceEnv() { static_cast<void>(nesso::test::EnvClear("NESSO_TRACE")); }
};

} // namespace

TEST_CASE("NESSO_TRACE=1 emits one json line per session", "[trace]")
{
    const ClearTraceEnv clear_env;
    REQUIRE(nesso::test::EnvClear("NESSO_TRACE"));

    {
        std::stringstream sink;
        const RestoreCerr restore;
        std::cerr.rdbuf(sink.rdbuf());
        {
            const nesso::TraceSession session;
            const nesso::ScopedStage stage{"parse"};
        }
        REQUIRE(sink.str().empty());
    }

    REQUIRE(nesso::test::EnvSet("NESSO_TRACE", "1"));

    std::stringstream sink;
    {
        const RestoreCerr restore;
        std::cerr.rdbuf(sink.rdbuf());
        {
            const nesso::TraceSession session;
        }
        {
            const nesso::TraceSession session;
            {
                const nesso::ScopedStage stage{"parse"};
            }
            {
                const nesso::ScopedStage stage{"embed"};
            }
        }
        {
            const nesso::TraceSession session;
            const nesso::ScopedStage stage{"corpus-read"};
        }
    }

    const std::string text = sink.str();
    const auto parse_at = text.find("\"name\":\"parse\"");
    const auto embed_at = text.find("\"name\":\"embed\"");
    REQUIRE(parse_at != std::string::npos);
    REQUIRE(embed_at != std::string::npos);
    REQUIRE(parse_at < embed_at);
    REQUIRE(text.contains("\"name\":\"corpus-read\""));
    REQUIRE(text.contains("\"peakRssBytes\":"));
    REQUIRE(std::count(text.begin(), text.end(), '\n') == 2);
}

TEST_CASE("ScopedStage drops recording when stage capacity is full", "[trace]")
{
    const ClearTraceEnv clear_env;
    REQUIRE(nesso::test::EnvSet("NESSO_TRACE", "1"));

    std::stringstream sink;
    {
        const RestoreCerr restore;
        std::cerr.rdbuf(sink.rdbuf());
        {
            const nesso::TraceSession session;
            for (int index = 0; index < 17; ++index)
            {
                const nesso::ScopedStage stage{"stage"};
            }
        }
    }

    const std::string text = sink.str();
    std::size_t stages = 0;
    for (std::size_t pos = 0; (pos = text.find("\"name\":\"stage\"", pos)) != std::string::npos; pos += 14)
    {
        ++stages;
    }
    // Fixed capacity 16: the 17th destructor drops instead of growing.
    REQUIRE(stages == 16);
}

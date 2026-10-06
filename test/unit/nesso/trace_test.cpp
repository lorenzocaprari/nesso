// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#include <catch2/catch_all.hpp>
#include <trace.hpp>

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
    ~ClearTraceEnv() { unsetenv("NESSO_TRACE"); }
};

} // namespace

TEST_CASE("NESSO_TRACE=1 emits one json line per session", "[trace]")
{
    const ClearTraceEnv clearEnv;
    unsetenv("NESSO_TRACE");

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

    REQUIRE(setenv("NESSO_TRACE", "1", 1) == 0);

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
    const auto parseAt = text.find("\"name\":\"parse\"");
    const auto embedAt = text.find("\"name\":\"embed\"");
    REQUIRE(parseAt != std::string::npos);
    REQUIRE(embedAt != std::string::npos);
    REQUIRE(parseAt < embedAt);
    REQUIRE(text.find("\"name\":\"corpus-read\"") != std::string::npos);
    REQUIRE(text.find("\"peakRssBytes\":") != std::string::npos);
    REQUIRE(std::count(text.begin(), text.end(), '\n') == 2);
}

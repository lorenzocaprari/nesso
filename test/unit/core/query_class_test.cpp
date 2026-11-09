// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_all.hpp>
#include <core/query_class.hpp>

#include <string>
#include <string_view>
#include <vector>

static void expectIdentifier(std::string_view token)
{
    INFO("token: " << token);
    CHECK(nesso::core::isIdentifierToken(token));
}

static void expectNotIdentifier(std::string_view token)
{
    INFO("token: " << token);
    CHECK_FALSE(nesso::core::isIdentifierToken(token));
}

TEST_CASE("identifier token patterns match ADR 0011", "[query][core]")
{
    SECTION("positive matches")
    {
        expectIdentifier("550e8400-e29b-41d4-a716-446655440000");
        expectIdentifier("0a000086");
        expectIdentifier("0xdeadbeef01");
        expectIdentifier("1234");
        expectIdentifier("blk_-1608999687919862906");
        expectIdentifier("ERR-5001");
        expectIdentifier("10.0.0.1");
        expectIdentifier("10.0.0.1:8080");
        expectIdentifier("::1");
        expectIdentifier("[2001:db8::1]");
        expectIdentifier("[2001:db8::1]:443");
        expectIdentifier("::ffff:10.0.0.1");
        expectIdentifier("/var/log/app.log");
        expectIdentifier("/a/b/");
    }

    SECTION("near-misses")
    {
        expectNotIdentifier("timeout");
        expectNotIdentifier("deadbeef");
        expectNotIdentifier("/a");
        expectNotIdentifier("a/b");
        expectNotIdentifier("C:\\Windows\\x");
        expectNotIdentifier("12");
        expectNotIdentifier("abc");
    }
}

TEST_CASE("tokenizeQuery strips quotes and splits on ASCII whitespace", "[query][core]")
{
    SECTION("whole-query quotes")
    {
        const nesso::core::QueryTokens tokens = nesso::core::tokenizeQuery("\"0a000086\"");
        REQUIRE(tokens.stripped == "0a000086");
        REQUIRE(tokens.tokens == std::vector<std::string>{"0a000086"});
    }

    SECTION("per-token quotes")
    {
        const nesso::core::QueryTokens tokens = nesso::core::tokenizeQuery("error \"ERR-5001\" today");
        REQUIRE(tokens.stripped == "error \"ERR-5001\" today");
        REQUIRE(tokens.tokens == std::vector<std::string>{"error", "ERR-5001", "today"});
    }

    SECTION("drops empty tokens after split")
    {
        const nesso::core::QueryTokens tokens = nesso::core::tokenizeQuery("  one   two  ");
        REQUIRE(tokens.stripped == "  one   two  ");
        REQUIRE(tokens.tokens == std::vector<std::string>{"one", "two"});
    }
}

TEST_CASE("classifyQuery routes identifier, mixed, and natural language", "[query][core]")
{
    using nesso::core::QueryClass;

    SECTION("identifier")
    {
        CHECK(nesso::core::classifyQuery("0a000086") == QueryClass::Identifier);
        CHECK(nesso::core::classifyQuery("blk_-1608999687919862906") == QueryClass::Identifier);
        CHECK(nesso::core::classifyQuery("\"0a000086\"") == QueryClass::Identifier);
    }

    SECTION("natural language")
    {
        CHECK(nesso::core::classifyQuery("timeout") == QueryClass::NaturalLanguage);
        CHECK(nesso::core::classifyQuery("auth failure") == QueryClass::NaturalLanguage);
    }

    SECTION("mixed")
    {
        CHECK(nesso::core::classifyQuery("connection failed with ERR-5001") == QueryClass::Mixed);
        CHECK(nesso::core::classifyQuery("error \"ERR-5001\" today") == QueryClass::Mixed);
    }

    SECTION("empty after tokenization is natural language")
    {
        CHECK(nesso::core::classifyQuery("") == QueryClass::NaturalLanguage);
        CHECK(nesso::core::classifyQuery("   \t") == QueryClass::NaturalLanguage);
    }
}

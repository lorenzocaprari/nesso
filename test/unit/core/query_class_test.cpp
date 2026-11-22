// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_all.hpp>
#include <core/query_class.hpp>

#include <string>
#include <string_view>
#include <vector>

static void ExpectIdentifier(std::string_view token)
{
    INFO("token: " << token);
    CHECK(nesso::core::IsIdentifierToken(token));
}

static void ExpectNotIdentifier(std::string_view token)
{
    INFO("token: " << token);
    CHECK_FALSE(nesso::core::IsIdentifierToken(token));
}

TEST_CASE("identifier token patterns match ADR 0011", "[query][core]")
{
    SECTION("positive matches")
    {
        ExpectIdentifier("550e8400-e29b-41d4-a716-446655440000");
        ExpectIdentifier("0a000086");
        ExpectIdentifier("0xdeadbeef01");
        ExpectIdentifier("1234");
        ExpectIdentifier("blk_-1608999687919862906");
        ExpectIdentifier("ERR-5001");
        ExpectIdentifier("10.0.0.1");
        ExpectIdentifier("10.0.0.1:8080");
        ExpectIdentifier("::1");
        ExpectIdentifier("[2001:db8::1]");
        ExpectIdentifier("[2001:db8::1]:443");
        ExpectIdentifier("::ffff:10.0.0.1");
        ExpectIdentifier("/var/log/app.log");
        ExpectIdentifier("/a/b/");
    }

    SECTION("underscore-segmented ids")
    {
        ExpectIdentifier("attempt_1445144423722_0020_r_000000_0");
        ExpectIdentifier("attempt_1445144423722_0020_m_000002_1");
        ExpectIdentifier("container_1445144423722_0020_01_000002");
        ExpectIdentifier("jvm_1445144423722_0020_m_000002");
        ExpectIdentifier("job_1445144423722_0020");
        ExpectIdentifier("application_1445144423722_0020");
        ExpectIdentifier("ab_x_1234");
    }

    SECTION("underscore-segmented near-misses")
    {
        ExpectNotIdentifier("user_name_list");
        ExpectNotIdentifier("foo_bar_2");
        ExpectNotIdentifier("log_123_45");
        ExpectNotIdentifier("a_1445144423722_0020");
        ExpectNotIdentifier("attempt__1445144423722_0020");
        ExpectNotIdentifier("attempt_1445144423722_0020_");
        ExpectNotIdentifier("attempt_x_y");
        ExpectNotIdentifier("attempt-1445144423722-0020");
        ExpectNotIdentifier("attempt_1445144423722_00.20");
        ExpectNotIdentifier("abcdefghijklmnopq_1445144423722_0020");
        ExpectNotIdentifier("task9_1445144423722_0020");
    }

    SECTION("near-misses")
    {
        ExpectNotIdentifier("timeout");
        ExpectNotIdentifier("deadbeef");
        ExpectNotIdentifier("/a");
        ExpectNotIdentifier("a/b");
        ExpectNotIdentifier("C:\\Windows\\x");
        ExpectNotIdentifier("12");
        ExpectNotIdentifier("abc");
        // Unbracketed IPv6:port is not an identifier (ports need [address]:port).
        ExpectNotIdentifier("1:2:3:4:5:6:7:8:8080");
        ExpectNotIdentifier("fe80::1:44321");
    }
}

TEST_CASE("TokenizeQuery strips quotes and splits on ASCII whitespace", "[query][core]")
{
    SECTION("whole-query quotes")
    {
        const nesso::core::QueryTokens tokens = nesso::core::TokenizeQuery("\"0a000086\"");
        REQUIRE(tokens.stripped == "0a000086");
        REQUIRE(tokens.tokens == std::vector<std::string>{"0a000086"});
    }

    SECTION("per-token quotes")
    {
        const nesso::core::QueryTokens tokens = nesso::core::TokenizeQuery("error \"ERR-5001\" today");
        REQUIRE(tokens.stripped == "error \"ERR-5001\" today");
        REQUIRE(tokens.tokens == std::vector<std::string>{"error", "ERR-5001", "today"});
    }

    SECTION("drops empty tokens after split")
    {
        const nesso::core::QueryTokens tokens = nesso::core::TokenizeQuery("  one   two  ");
        REQUIRE(tokens.stripped == "  one   two  ");
        REQUIRE(tokens.tokens == std::vector<std::string>{"one", "two"});
    }
}

TEST_CASE("ClassifyQuery routes identifier, mixed, and natural language", "[query][core]")
{
    using nesso::core::QueryClass;

    SECTION("identifier")
    {
        CHECK(nesso::core::ClassifyQuery("0a000086") == QueryClass::Identifier);
        CHECK(nesso::core::ClassifyQuery("blk_-1608999687919862906") == QueryClass::Identifier);
        CHECK(nesso::core::ClassifyQuery("\"0a000086\"") == QueryClass::Identifier);
        CHECK(nesso::core::ClassifyQuery("attempt_1445144423722_0020_m_000002_1") == QueryClass::Identifier);
    }

    SECTION("natural language")
    {
        CHECK(nesso::core::ClassifyQuery("timeout") == QueryClass::NaturalLanguage);
        CHECK(nesso::core::ClassifyQuery("auth failure") == QueryClass::NaturalLanguage);
    }

    SECTION("mixed")
    {
        CHECK(nesso::core::ClassifyQuery("connection failed with ERR-5001") == QueryClass::Mixed);
        CHECK(nesso::core::ClassifyQuery("error \"ERR-5001\" today") == QueryClass::Mixed);
        CHECK(nesso::core::ClassifyQuery("container_1445144423722_0020_01_000002 killed") == QueryClass::Mixed);
    }

    SECTION("empty after tokenization is natural language")
    {
        CHECK(nesso::core::ClassifyQuery("") == QueryClass::NaturalLanguage);
        CHECK(nesso::core::ClassifyQuery("   \t") == QueryClass::NaturalLanguage);
    }
}

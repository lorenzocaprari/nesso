// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_all.hpp>
#include <core/lexical_terms.hpp>

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

static std::vector<std::string> termsOf(std::string_view text) { return nesso::core::lexicalTerms(text); }

TEST_CASE("lexical term rules are version v1", "[lexical]") { CHECK(nesso::core::LEXICAL_TERMS_VERSION == "v1"); }

TEST_CASE("lexical terms lowercase ASCII", "[lexical]")
{
    CHECK(termsOf("Hello") == std::vector<std::string>{"hello"});
    CHECK(termsOf("Hello, WORLD!") == std::vector<std::string>{"hello", "world"});
    CHECK(termsOf("HTTP") == std::vector<std::string>{"http"});
}

TEST_CASE("lexical terms split camelCase before lowercasing", "[lexical]")
{
    // Boundaries are read from the original bytes. `fooBar` is `foo` then `bar`.
    CHECK(termsOf("fooBar") == std::vector<std::string>{"foo", "bar"});
    CHECK(termsOf("fooBarBaz") == std::vector<std::string>{"foo", "bar", "baz"});
    CHECK(termsOf("simpleXML") == std::vector<std::string>{"simple", "xml"});
    // Uppercase runs stay one piece. Splitting them would break `IPv4`.
    CHECK(termsOf("XMLParser") == std::vector<std::string>{"xmlparser"});
    CHECK(termsOf("getHTTPResponse") == std::vector<std::string>{"get", "httpresponse"});
    CHECK(termsOf("IPv4") == std::vector<std::string>{"ipv4"});
    // A single leading capital is not a boundary. The piece is then lowercased.
    CHECK(termsOf("Ab") == std::vector<std::string>{"ab"});
    CHECK(termsOf("aB") == std::vector<std::string>{"a", "b"});
    // Digits stay inside the piece. They do not open a camelCase boundary.
    CHECK(termsOf("x1") == std::vector<std::string>{"x1"});
    CHECK(termsOf("foo2Bar") == std::vector<std::string>{"foo2bar"});
}

TEST_CASE("lexical terms keep joiner compounds whole", "[lexical]")
{
    CHECK(termsOf("blk_-1608999687919862906") ==
          std::vector<std::string>{"blk", "1608999687919862906", "blk_-1608999687919862906"});
    CHECK(termsOf("10.0.0.1:50010") == std::vector<std::string>{"10", "0", "0", "1", "50010", "10.0.0.1:50010"});
    CHECK(termsOf("ERR-5001") == std::vector<std::string>{"err", "5001", "err-5001"});
    // `/` is not a joiner, so the path is three terms and no compound.
    CHECK(termsOf("/var/log/x1") == std::vector<std::string>{"var", "log", "x1"});
    CHECK(termsOf("fooBar-baz") == std::vector<std::string>{"foo", "bar", "baz", "foobar-baz"});
    CHECK(termsOf("foo--bar") == std::vector<std::string>{"foo", "bar", "foo--bar"});
    CHECK(termsOf("foo-") == std::vector<std::string>{"foo"});
    CHECK(termsOf("-foo") == std::vector<std::string>{"foo"});
    // One entry per occurrence: the atomic term is repeated, then the compound.
    CHECK(termsOf("a-a") == std::vector<std::string>{"a", "a", "a-a"});
    CHECK(termsOf("cat cat") == std::vector<std::string>{"cat", "cat"});
}

TEST_CASE("lexical terms keep bytes at or above 0x80", "[lexical]")
{
    const std::string cafe = std::string("caf") + "\xC3\xA9";
    CHECK(termsOf(cafe) == std::vector<std::string>{cafe});

    const std::string mixed = std::string("A") + "\xC3\xA9";
    CHECK(termsOf(mixed) == std::vector<std::string>{std::string("a") + "\xC3\xA9"});

    const std::string split = std::string("\xC3\xA9") + " " + "\x80";
    CHECK(termsOf(split) == std::vector<std::string>{"\xC3\xA9", "\x80"});

    const std::string joined = std::string("\xFF") + "-" + "\x80";
    CHECK(termsOf(joined) == std::vector<std::string>{"\xFF", "\x80", joined});

    // 0x7F is a separator, not a term byte.
    const std::string deleted = std::string("a") + "\x7F" + "b";
    CHECK(termsOf(deleted) == std::vector<std::string>{"a", "b"});
}

TEST_CASE("lexical terms of empty input are empty", "[lexical]")
{
    CHECK(termsOf("").empty());
    CHECK(termsOf(std::string_view{}).empty());
}

TEST_CASE("lexical terms of separators only are empty", "[lexical]")
{
    CHECK(termsOf(" \t\n/\\,;").empty());
    CHECK(termsOf("-_.:").empty());
    CHECK(termsOf("---").empty());
    CHECK(termsOf(" /._:").empty());
}

TEST_CASE("lexical terms scan a very long input", "[lexical]")
{
    constexpr std::size_t length = 100000;
    const std::string letters(length, 'A');
    const std::vector<std::string> one = termsOf(letters);
    REQUIRE(one.size() == 1);
    CHECK(one[0].size() == length);
    CHECK(std::ranges::all_of(one[0], [](char byte) { return byte == 'a'; }));

    constexpr std::size_t groups = 20000;
    std::string chained;
    chained.reserve((groups * 2) + 1);
    for (std::size_t group = 0; group < groups; ++group)
    {
        chained += "a-";
    }
    chained.push_back('b');
    const std::vector<std::string> many = termsOf(chained);
    REQUIRE(many.size() == groups + 2);
    std::size_t atoms = 0;
    for (const std::string &term : many)
    {
        if (term == "a")
        {
            ++atoms;
        }
    }
    CHECK(atoms == groups);
    CHECK(many[groups] == "b");
    CHECK(many.back() == chained);
}

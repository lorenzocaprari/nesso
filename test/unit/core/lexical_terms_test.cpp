// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_all.hpp>
#include <core/lexical_terms.hpp>

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

static auto TermsOf(std::string_view text) -> std::vector<std::string> { return nesso::core::LexicalTerms(text); }

TEST_CASE("lexical term rules are version v1", "[lexical]") { CHECK(nesso::core::LEXICAL_TERMS_VERSION == "v1"); }

TEST_CASE("lexical terms lowercase ASCII", "[lexical]")
{
    CHECK(TermsOf("Hello") == std::vector<std::string>{"hello"});
    CHECK(TermsOf("Hello, WORLD!") == std::vector<std::string>{"hello", "world"});
    CHECK(TermsOf("HTTP") == std::vector<std::string>{"http"});
}

TEST_CASE("lexical terms split camelCase before lowercasing", "[lexical]")
{
    // Boundaries are read from the original bytes. `fooBar` is `foo` then `bar`.
    CHECK(TermsOf("fooBar") == std::vector<std::string>{"foo", "bar"});
    CHECK(TermsOf("fooBarBaz") == std::vector<std::string>{"foo", "bar", "baz"});
    CHECK(TermsOf("simpleXML") == std::vector<std::string>{"simple", "xml"});
    // Uppercase runs stay one piece. Splitting them would break `IPv4`.
    CHECK(TermsOf("XMLParser") == std::vector<std::string>{"xmlparser"});
    CHECK(TermsOf("getHTTPResponse") == std::vector<std::string>{"get", "httpresponse"});
    CHECK(TermsOf("IPv4") == std::vector<std::string>{"ipv4"});
    // A single leading capital is not a boundary. The piece is then lowercased.
    CHECK(TermsOf("Ab") == std::vector<std::string>{"ab"});
    CHECK(TermsOf("aB") == std::vector<std::string>{"a", "b"});
    // Digits stay inside the piece. They do not open a camelCase boundary.
    CHECK(TermsOf("x1") == std::vector<std::string>{"x1"});
    CHECK(TermsOf("foo2Bar") == std::vector<std::string>{"foo2bar"});
}

TEST_CASE("lexical terms keep joiner compounds whole", "[lexical]")
{
    CHECK(TermsOf("blk_-1608999687919862906") ==
          std::vector<std::string>{"blk", "1608999687919862906", "blk_-1608999687919862906"});
    CHECK(TermsOf("10.0.0.1:50010") == std::vector<std::string>{"10", "0", "0", "1", "50010", "10.0.0.1:50010"});
    CHECK(TermsOf("ERR-5001") == std::vector<std::string>{"err", "5001", "err-5001"});
    // `/` is not a joiner, so the path is three terms and no compound.
    CHECK(TermsOf("/var/log/x1") == std::vector<std::string>{"var", "log", "x1"});
    CHECK(TermsOf("fooBar-baz") == std::vector<std::string>{"foo", "bar", "baz", "foobar-baz"});
    CHECK(TermsOf("foo--bar") == std::vector<std::string>{"foo", "bar", "foo--bar"});
    CHECK(TermsOf("foo-") == std::vector<std::string>{"foo"});
    CHECK(TermsOf("-foo") == std::vector<std::string>{"foo"});
    // One entry per occurrence: the atomic term is repeated, then the compound.
    CHECK(TermsOf("a-a") == std::vector<std::string>{"a", "a", "a-a"});
    CHECK(TermsOf("cat cat") == std::vector<std::string>{"cat", "cat"});
}

TEST_CASE("lexical terms keep bytes at or above 0x80", "[lexical]")
{
    const std::string cafe = std::string("caf") + "\xC3\xA9";
    CHECK(TermsOf(cafe) == std::vector<std::string>{cafe});

    const std::string mixed = std::string("A") + "\xC3\xA9";
    CHECK(TermsOf(mixed) == std::vector<std::string>{std::string("a") + "\xC3\xA9"});

    const std::string split = std::string("\xC3\xA9") + " " + "\x80";
    CHECK(TermsOf(split) == std::vector<std::string>{"\xC3\xA9", "\x80"});

    const std::string joined = std::string("\xFF") + "-" + "\x80";
    CHECK(TermsOf(joined) == std::vector<std::string>{"\xFF", "\x80", joined});

    // 0x7F is a separator, not a term byte.
    const std::string deleted = std::string("a") + "\x7F" + "b";
    CHECK(TermsOf(deleted) == std::vector<std::string>{"a", "b"});
}

TEST_CASE("lexical terms of empty input are empty", "[lexical]")
{
    CHECK(TermsOf("").empty());
    CHECK(TermsOf(std::string_view{}).empty());
}

TEST_CASE("term byte order treats bytes as unsigned", "[lexical]")
{
    using nesso::core::TermByteLess;
    // Signed char would put 0x80 before ASCII; unsigned puts it after.
    CHECK(TermByteLess("a", "\x80"));
    CHECK_FALSE(TermByteLess("\x80", "a"));
    CHECK(TermByteLess("\x7f", "\x80"));
    CHECK_FALSE(TermByteLess("\x80", "\x7f"));
}

TEST_CASE("term byte order puts a proper prefix first", "[lexical]")
{
    using nesso::core::TermByteLess;
    CHECK(TermByteLess("ab", "abc"));
    CHECK_FALSE(TermByteLess("abc", "ab"));
}

TEST_CASE("term byte order is irreflexive", "[lexical]")
{
    using nesso::core::TermByteLess;
    CHECK_FALSE(TermByteLess("cat", "cat"));
    CHECK_FALSE(TermByteLess("\x80\xFF", "\x80\xFF"));
}

TEST_CASE("term byte order ranks the empty string", "[lexical]")
{
    using nesso::core::TermByteLess;
    CHECK(TermByteLess("", "a"));
    CHECK_FALSE(TermByteLess("a", ""));
    CHECK_FALSE(TermByteLess("", ""));
}

TEST_CASE("lexical terms of separators only are empty", "[lexical]")
{
    CHECK(TermsOf(" \t\n/\\,;").empty());
    CHECK(TermsOf("-_.:").empty());
    CHECK(TermsOf("---").empty());
    CHECK(TermsOf(" /._:").empty());
}

TEST_CASE("lexical terms scan a very long input", "[lexical]")
{
    constexpr std::size_t length = 100000;
    const std::string letters(length, 'A');
    const std::vector<std::string> one = TermsOf(letters);
    REQUIRE(one.size() == 1);
    CHECK(one[0].size() == length);
    CHECK(std::ranges::all_of(one[0], [](char byte) -> bool { return byte == 'a'; }));

    constexpr std::size_t groups = 20000;
    std::string chained;
    chained.reserve((groups * 2) + 1);
    for (std::size_t group = 0; group < groups; ++group)
    {
        chained += "a-";
    }
    chained.push_back('b');
    const std::vector<std::string> many = TermsOf(chained);
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

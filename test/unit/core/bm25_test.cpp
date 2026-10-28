// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_all.hpp>
#include <core/bm25.hpp>

#include <array>
#include <bit>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string_view>
#include <vector>

static void addTokens(nesso::core::Bm25Index &index, std::initializer_list<std::string_view> tokens)
{
    const std::vector<std::string_view> line(tokens);
    index.addLine(line);
}

static void expectBits(double value, std::uint64_t bits) { CHECK(std::bit_cast<std::uint64_t>(value) == bits); }

static nesso::core::Bm25Index threeLineCorpus()
{
    // line 0: cat elf   |D| = 2
    // line 1: cat       |D| = 1
    // line 2: dog       |D| = 1
    // N = 3, total length 4, avgdl = 4/3
    // df(cat) = 2, df(elf) = 1, df(dog) = 1
    nesso::core::Bm25Index index;
    addTokens(index, {"cat", "elf"});
    addTokens(index, {"cat"});
    addTokens(index, {"dog"});
    return index;
}

TEST_CASE("BM25 matches the hand-computed scores of a 3-line corpus", "[bm25]")
{
    // k1 = 1.5, b = 0.75, avgdl = 4/3.
    // idf(cat) = ln(1 + (3 - 2 + 0.5) / (2 + 0.5)) = ln(1.6)
    //          = 0.47000362924573563
    // idf(elf) = ln(1 + (3 - 1 + 0.5) / (1 + 0.5)) = ln(8/3)
    //          = 0.9808292530117263
    //
    // Line 0, tf(cat) = 1, |D| = 2:
    //   norm = (1 - 0.75) + (0.75 * (2 / (4/3))) = 1.375
    //   denom = 1 + 1.5 * 1.375 = 3.0625
    //   weight = (1 * 2.5) / 3.0625
    //   score = ln(1.6) * 2.5 / 3.0625 = 0.3836764320373352
    //
    // Line 1, tf(cat) = 1, |D| = 1:
    //   norm = 0.25 + (0.75 * (1 / (4/3))) = 0.8125
    //   denom = 1 + 1.5 * 0.8125 = 2.21875
    //   score = ln(1.6) * 2.5 / 2.21875 = 0.5295815540797022
    //
    // Line 2 has no cat, so its score is 0.
    //
    // Query elf then cat. Distinct terms in unsigned byte order are cat, then elf.
    // Line 0 also adds elf, tf = 1, |D| = 2:
    //   the cat norm above is 1.375, so elf weight = 2.5 / 3.0625
    //   elf = ln(8/3) * 2.5 / 3.0625 = 0.8006769412340623
    //   sum = 0.3836764320373352 + 0.8006769412340623 = 1.1843533732713976
    // For these two contributions the opposite order is the same double.
    const nesso::core::Bm25Index index = threeLineCorpus();
    CHECK(index.lineCount() == 3);
    CHECK(index.averageLength() == 4.0 / 3.0);
    CHECK(nesso::core::BM25_K1 == 1.5);
    CHECK(nesso::core::BM25_B == 0.75);

    const std::array<std::string_view, 1> cat{"cat"};
    const std::vector<double> catScores = index.scores(cat);
    REQUIRE(catScores.size() == 3);
    expectBits(catScores[0], 0x3fd88e2797f62a21ULL);
    expectBits(catScores[1], 0x3fe0f25503eaca26ULL);
    expectBits(catScores[2], 0x0ULL);

    const std::array<std::string_view, 2> elfThenCat{"elf", "cat"};
    const std::vector<double> both = index.scores(elfThenCat);
    REQUIRE(both.size() == 3);
    expectBits(both[0], 0x3ff2f31c85d1ba18ULL);
    expectBits(both[1], 0x3fe0f25503eaca26ULL);
    expectBits(both[2], 0x0ULL);
}

TEST_CASE("BM25 sums distinct query terms in unsigned byte order", "[bm25]")
{
    // line 0: (empty)          |D| = 0
    // line 1: aa aa            |D| = 2, tf(aa) = 2
    // line 2: aa aa bb cc      |D| = 4, tf(aa) = 2, tf(bb) = 1, tf(cc) = 1
    // N = 3, total length 6, avgdl = 2
    // df(aa) = 2, df(bb) = 1, df(cc) = 1
    // idf(aa) = ln(1.6), idf(bb) = idf(cc) = ln(8/3)
    //
    // Line 2 norm = (1 - 0.75) + (0.75 * (4 / 2)) = 1.75
    //   aa: denom = 2 + 1.5 * 1.75 = 4.625, part = ln(1.6) * 5 / 4.625
    //   bb: denom = 1 + 1.5 * 1.75 = 3.625, part = ln(8/3) * 2.5 / 3.625
    //   cc: the same part as bb
    // Byte order is aa, bb, cc. That sum is 1.8609799668055997 (0x3ffdc692edff0f39).
    // The reverse fold cc, bb, aa is 0x3ffdc692edff0f38.
    // The query is passed in reverse so a sum in input order would take the other bits.
    nesso::core::Bm25Index index;
    addTokens(index, {});
    addTokens(index, {"aa", "aa"});
    addTokens(index, {"aa", "aa", "bb", "cc"});
    CHECK(index.averageLength() == 2.0);

    const std::array<std::string_view, 3> reverse{"cc", "bb", "aa"};
    const std::vector<double> scores = index.scores(reverse);
    REQUIRE(scores.size() == 3);
    expectBits(scores[0], 0x0ULL);
    expectBits(scores[1], 0x3fe57c62a4f764ddULL);
    expectBits(scores[2], 0x3ffdc692edff0f39ULL);
}

TEST_CASE("a query term missing from the corpus adds nothing", "[bm25]")
{
    const nesso::core::Bm25Index index = threeLineCorpus();
    const std::array<std::string_view, 1> missing{"zzz"};
    const std::vector<double> none = index.scores(missing);
    REQUIRE(none.size() == 3);
    CHECK(none == std::vector<double>{0.0, 0.0, 0.0});

    const std::array<std::string_view, 2> catAndMissing{"cat", "zzz"};
    const std::array<std::string_view, 1> cat{"cat"};
    const std::vector<double> withCat = index.scores(catAndMissing);
    const std::vector<double> onlyCat = index.scores(cat);
    REQUIRE(withCat.size() == onlyCat.size());
    for (std::size_t line = 0; line < withCat.size(); ++line)
    {
        expectBits(withCat[line], std::bit_cast<std::uint64_t>(onlyCat[line]));
    }

    // "a" is a prefix of "ab", so ordering them takes the length branch.
    // Neither term is in the corpus.
    const std::array<std::string_view, 2> prefix{"ab", "a"};
    const std::vector<double> absent = index.scores(prefix);
    CHECK(absent == std::vector<double>{0.0, 0.0, 0.0});
}

TEST_CASE("a repeated query term is counted once", "[bm25]")
{
    const nesso::core::Bm25Index index = threeLineCorpus();
    const std::array<std::string_view, 2> repeated{"cat", "cat"};
    const std::array<std::string_view, 1> once{"cat"};
    const std::vector<double> many = index.scores(repeated);
    const std::vector<double> single = index.scores(once);
    REQUIRE(many.size() == single.size());
    for (std::size_t line = 0; line < many.size(); ++line)
    {
        expectBits(many[line], std::bit_cast<std::uint64_t>(single[line]));
    }
}

TEST_CASE("zero-length lines score zero and do not divide by avgdl", "[bm25]")
{
    // line 0: empty   |D| = 0
    // line 1: cat     |D| = 1
    // line 2: empty   |D| = 0
    // N = 3, total length 1, avgdl = 1/3, df(cat) = 1
    // idf(cat) = ln(8/3)
    // line 1 norm = 0.25 + (0.75 * (1 / (1/3))) = 2.5
    // denom = 1 + 1.5 * 2.5 = 4.75
    // score = ln(8/3) * 2.5 / 4.75 = 0.51622592263775069
    nesso::core::Bm25Index mixed;
    addTokens(mixed, {});
    addTokens(mixed, {"cat"});
    addTokens(mixed, {});
    CHECK(mixed.lineCount() == 3);
    CHECK(mixed.averageLength() == 1.0 / 3.0);
    const std::array<std::string_view, 1> cat{"cat"};
    const std::vector<double> scores = mixed.scores(cat);
    REQUIRE(scores.size() == 3);
    expectBits(scores[0], 0x0ULL);
    expectBits(scores[1], 0x3fe084ec39e27339ULL);
    expectBits(scores[2], 0x0ULL);

    nesso::core::Bm25Index emptyLines;
    addTokens(emptyLines, {});
    addTokens(emptyLines, {});
    CHECK(emptyLines.averageLength() == 0.0);
    const std::vector<double> zeros = emptyLines.scores(cat);
    REQUIRE(zeros.size() == 2);
    CHECK(zeros[0] == 0.0);
    CHECK(zeros[1] == 0.0);
    CHECK(emptyLines.scores(std::span<const std::string_view>{}).size() == 2);

    nesso::core::Bm25Index none;
    CHECK(none.lineCount() == 0);
    CHECK(none.averageLength() == 0.0);
    CHECK(none.scores(cat).empty());
}

TEST_CASE("tied BM25 scores stay on their line ordinals", "[bm25]")
{
    // line 0: red blue   |D| = 2
    // line 1: blue red   |D| = 2, same multiset as line 0
    // line 2: red        |D| = 1
    // N = 3, total length 5, avgdl = 5/3, df(red) = 3
    // idf(red) = ln(1 + (3 - 3 + 0.5) / (3 + 0.5)) = ln(8/7)
    //          = 0.13353139262452257
    // Lines 0 and 1, tf = 1, |D| = 2:
    //   norm = 0.25 + (0.75 * (2 / (5/3))) = 1.15
    //   denom = 1 + 1.5 * 1.15 = 2.725
    //   score = ln(8/7) * 2.5 / 2.725 = 0.12250586479313999
    // Line 2, tf = 1, |D| = 1:
    //   norm = 0.25 + (0.75 * (1 / (5/3))) = 0.7
    //   denom = 1 + 1.5 * 0.7 = 2.05
    //   score = ln(8/7) * 2.5 / 2.05 = 0.16284316173722269
    // Line 2 outscores the tie, and it stays at index 2.
    nesso::core::Bm25Index index;
    addTokens(index, {"red", "blue"});
    addTokens(index, {"blue", "red"});
    addTokens(index, {"red"});
    CHECK(index.averageLength() == 5.0 / 3.0);

    const std::array<std::string_view, 1> red{"red"};
    const std::vector<double> scores = index.scores(red);
    REQUIRE(scores.size() == 3);
    expectBits(scores[0], 0x3fbf5c8b5adacfdaULL);
    expectBits(scores[1], 0x3fbf5c8b5adacfdaULL);
    expectBits(scores[2], 0x3fc4d80b7304f12cULL);
    CHECK(scores[0] == scores[1]);
    CHECK(scores[2] > scores[0]);

    const std::vector<double> again = index.scores(red);
    REQUIRE(again.size() == scores.size());
    for (std::size_t line = 0; line < scores.size(); ++line)
    {
        expectBits(again[line], std::bit_cast<std::uint64_t>(scores[line]));
    }
}

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

static void AddTokens(nesso::core::Bm25Index &index, std::initializer_list<std::string_view> tokens)
{
    const std::vector<std::string_view> line(tokens);
    index.AddLine(line);
}

static void ExpectBits(double value, std::uint64_t bits) { CHECK(std::bit_cast<std::uint64_t>(value) == bits); }

static nesso::core::Bm25Index ThreeLineCorpus()
{
    // line 0: cat elf   |D| = 2
    // line 1: cat       |D| = 1
    // line 2: dog       |D| = 1
    // N = 3, total length 4, avgdl = 4/3
    // df(cat) = 2, df(elf) = 1, df(dog) = 1
    nesso::core::Bm25Index index;
    AddTokens(index, {"cat", "elf"});
    AddTokens(index, {"cat"});
    AddTokens(index, {"dog"});
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
    // Query elf then cat. Distinct terms in unsigned byte order are cat, then
    // elf. Line 0 also adds elf, tf = 1, |D| = 2:
    //   the cat norm above is 1.375, so elf weight = 2.5 / 3.0625
    //   elf = ln(8/3) * 2.5 / 3.0625 = 0.8006769412340623
    //   sum = 0.3836764320373352 + 0.8006769412340623 = 1.1843533732713976
    // For these two contributions the opposite order is the same double.
    const nesso::core::Bm25Index index = ThreeLineCorpus();
    CHECK(index.LineCount() == 3);
    CHECK(index.AverageLength() == 4.0 / 3.0);
    CHECK(nesso::core::BM25_K1 == 1.5);
    CHECK(nesso::core::BM25_B == 0.75);

    const std::array<std::string_view, 1> cat{"cat"};
    const std::vector<double> cat_scores = index.Scores(cat);
    REQUIRE(cat_scores.size() == 3);
    ExpectBits(cat_scores[0], 0x3fd88e2797f62a21ULL);
    ExpectBits(cat_scores[1], 0x3fe0f25503eaca26ULL);
    ExpectBits(cat_scores[2], 0x0ULL);

    const std::array<std::string_view, 2> elf_then_cat{"elf", "cat"};
    const std::vector<double> both = index.Scores(elf_then_cat);
    REQUIRE(both.size() == 3);
    ExpectBits(both[0], 0x3ff2f31c85d1ba18ULL);
    ExpectBits(both[1], 0x3fe0f25503eaca26ULL);
    ExpectBits(both[2], 0x0ULL);
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
    // Byte order is aa, bb, cc. That sum is 1.8609799668055997
    // (0x3ffdc692edff0f39). The reverse fold cc, bb, aa is 0x3ffdc692edff0f38.
    // The query is passed in reverse so a sum in input order would take the other
    // bits.
    nesso::core::Bm25Index index;
    AddTokens(index, {});
    AddTokens(index, {"aa", "aa"});
    AddTokens(index, {"aa", "aa", "bb", "cc"});
    CHECK(index.AverageLength() == 2.0);

    const std::array<std::string_view, 3> reverse{"cc", "bb", "aa"};
    const std::vector<double> scores = index.Scores(reverse);
    REQUIRE(scores.size() == 3);
    ExpectBits(scores[0], 0x0ULL);
    ExpectBits(scores[1], 0x3fe57c62a4f764ddULL);
    ExpectBits(scores[2], 0x3ffdc692edff0f39ULL);
}

TEST_CASE("a query term missing from the corpus adds nothing", "[bm25]")
{
    const nesso::core::Bm25Index index = ThreeLineCorpus();
    const std::array<std::string_view, 1> missing{"zzz"};
    const std::vector<double> none = index.Scores(missing);
    REQUIRE(none.size() == 3);
    CHECK(none == std::vector<double>{0.0, 0.0, 0.0});

    const std::array<std::string_view, 2> cat_and_missing{"cat", "zzz"};
    const std::array<std::string_view, 1> cat{"cat"};
    const std::vector<double> with_cat = index.Scores(cat_and_missing);
    const std::vector<double> only_cat = index.Scores(cat);
    REQUIRE(with_cat.size() == only_cat.size());
    for (std::size_t line = 0; line < with_cat.size(); ++line)
    {
        ExpectBits(with_cat[line], std::bit_cast<std::uint64_t>(only_cat[line]));
    }

    // "a" is a prefix of "ab", so ordering them takes the length branch.
    // Neither term is in the corpus.
    const std::array<std::string_view, 2> prefix{"ab", "a"};
    const std::vector<double> absent = index.Scores(prefix);
    CHECK(absent == std::vector<double>{0.0, 0.0, 0.0});
}

TEST_CASE("a repeated query term is counted once", "[bm25]")
{
    const nesso::core::Bm25Index index = ThreeLineCorpus();
    const std::array<std::string_view, 2> repeated{"cat", "cat"};
    const std::array<std::string_view, 1> once{"cat"};
    const std::vector<double> many = index.Scores(repeated);
    const std::vector<double> single = index.Scores(once);
    REQUIRE(many.size() == single.size());
    for (std::size_t line = 0; line < many.size(); ++line)
    {
        ExpectBits(many[line], std::bit_cast<std::uint64_t>(single[line]));
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
    AddTokens(mixed, {});
    AddTokens(mixed, {"cat"});
    AddTokens(mixed, {});
    CHECK(mixed.LineCount() == 3);
    CHECK(mixed.AverageLength() == 1.0 / 3.0);
    const std::array<std::string_view, 1> cat{"cat"};
    const std::vector<double> scores = mixed.Scores(cat);
    REQUIRE(scores.size() == 3);
    ExpectBits(scores[0], 0x0ULL);
    ExpectBits(scores[1], 0x3fe084ec39e27339ULL);
    ExpectBits(scores[2], 0x0ULL);

    nesso::core::Bm25Index empty_lines;
    AddTokens(empty_lines, {});
    AddTokens(empty_lines, {});
    CHECK(empty_lines.AverageLength() == 0.0);
    const std::vector<double> zeros = empty_lines.Scores(cat);
    REQUIRE(zeros.size() == 2);
    CHECK(zeros[0] == 0.0);
    CHECK(zeros[1] == 0.0);
    CHECK(empty_lines.Scores(std::span<const std::string_view>{}).size() == 2);

    nesso::core::Bm25Index none;
    CHECK(none.LineCount() == 0);
    CHECK(none.AverageLength() == 0.0);
    CHECK(none.Scores(cat).empty());
}

TEST_CASE("AddLinePostings matches AddLine bit-for-bit", "[bm25]")
{
    nesso::core::Bm25Index from_strings = ThreeLineCorpus();

    nesso::core::Bm25Index from_ids;
    const std::uint32_t cat = from_ids.InternTerm("cat");
    const std::uint32_t elf = from_ids.InternTerm("elf");
    const std::uint32_t dog = from_ids.InternTerm("dog");
    const std::array<std::pair<std::uint32_t, std::uint32_t>, 2> line0{{{cat, 1}, {elf, 1}}};
    const std::array<std::pair<std::uint32_t, std::uint32_t>, 1> line1{{{cat, 1}}};
    const std::array<std::pair<std::uint32_t, std::uint32_t>, 1> line2{{{dog, 1}}};
    from_ids.AddLinePostings(line0, 2);
    from_ids.AddLinePostings(line1, 1);
    from_ids.AddLinePostings(line2, 1);

    const std::array<std::string_view, 2> query{"elf", "cat"};
    const std::vector<double> left = from_strings.Scores(query);
    const std::vector<double> right = from_ids.Scores(query);
    REQUIRE(left.size() == right.size());
    for (std::size_t line = 0; line < left.size(); ++line)
    {
        ExpectBits(right[line], std::bit_cast<std::uint64_t>(left[line]));
    }
}

TEST_CASE("ScoresFromPostings matches Bm25Index for a sorted dictionary", "[bm25]")
{
    // Dictionary ids follow unsigned-byte order: cat=0, dog=1, elf=2.
    const std::array<std::string_view, 3> dictionary{"cat", "dog", "elf"};
    const std::array<std::pair<std::uint32_t, std::uint32_t>, 2> row0{{{0, 1}, {2, 1}}};
    const std::array<std::pair<std::uint32_t, std::uint32_t>, 1> row1{{{0, 1}}};
    const std::array<std::pair<std::uint32_t, std::uint32_t>, 1> row2{{{1, 1}}};
    const std::array<nesso::core::Bm25LinePostings, 3> lines{
        nesso::core::Bm25LinePostings{.length = 2, .entries = row0},
        nesso::core::Bm25LinePostings{.length = 1, .entries = row1},
        nesso::core::Bm25LinePostings{.length = 1, .entries = row2},
    };

    const nesso::core::Bm25Index index = ThreeLineCorpus();
    const std::array<std::string_view, 2> query{"elf", "cat"};
    const std::vector<double> from_index = index.Scores(query);
    const std::vector<double> from_postings = nesso::core::ScoresFromPostings(dictionary, lines, query);
    REQUIRE(from_index.size() == from_postings.size());
    for (std::size_t line = 0; line < from_index.size(); ++line)
    {
        ExpectBits(from_postings[line], std::bit_cast<std::uint64_t>(from_index[line]));
    }
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
    AddTokens(index, {"red", "blue"});
    AddTokens(index, {"blue", "red"});
    AddTokens(index, {"red"});
    CHECK(index.AverageLength() == 5.0 / 3.0);

    const std::array<std::string_view, 1> red{"red"};
    const std::vector<double> scores = index.Scores(red);
    REQUIRE(scores.size() == 3);
    ExpectBits(scores[0], 0x3fbf5c8b5adacfdaULL);
    ExpectBits(scores[1], 0x3fbf5c8b5adacfdaULL);
    ExpectBits(scores[2], 0x3fc4d80b7304f12cULL);
    CHECK(scores[0] == scores[1]);
    CHECK(scores[2] > scores[0]);

    const std::vector<double> again = index.Scores(red);
    REQUIRE(again.size() == scores.size());
    for (std::size_t line = 0; line < scores.size(); ++line)
    {
        ExpectBits(again[line], std::bit_cast<std::uint64_t>(scores[line]));
    }
}

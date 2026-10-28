// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#ifndef CORE_BM25_HPP
#define CORE_BM25_HPP

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace nesso::core
{

/// Okapi BM25 `k1`.
inline constexpr double BM25_K1 = 1.5;

/// Okapi BM25 `b`.
inline constexpr double BM25_B = 0.75;

/// In-memory Okapi BM25 over lines.
///
/// `idf = ln(1 + (N - df + 0.5) / (df + 0.5))`, with `std::log`. `|D|` is the
/// number of tokens passed to `addLine` for that line. `df` is the number of
/// lines that contain the term. `N` is `lineCount()`. `avgdl` is
/// `averageLength()`.
///
/// `scores` sums in double over the distinct query terms in unsigned byte
/// order. A repeated query term counts once. A term present in no line adds
/// nothing. The returned vector has one score per line, in `addLine` order.
/// It does not rank: equal scores stay equal, and a higher score does not move
/// toward the front. Breaking a tie by line ordinal is the caller's job.
///
/// `addLine` copies the tokens, so later postings storage can feed a line's
/// term multiset without the original text. Scoring starts no threads and does
/// no parallel reduction, so the scores do not depend on a thread count.
class Bm25Index
{
  public:
    /// Appends one line. Each token counts once toward `|D|` and toward that
    /// term's frequency. An empty span is a zero-length line and still counts in `N`.
    void addLine(std::span<const std::string_view> terms);

    /// Lines added so far.
    [[nodiscard]] std::size_t lineCount() const noexcept;

    /// Mean `|D|`, or `0` when `lineCount()` is `0`.
    [[nodiscard]] double averageLength() const noexcept;

    /// One score per added line, in insertion order.
    [[nodiscard]] std::vector<double> scores(std::span<const std::string_view> query) const;

  private:
    struct Line
    {
        // Unique terms, unsigned-byte order, with the frequency of each.
        std::vector<std::pair<std::string, std::size_t>> frequencies;
        std::size_t length = 0;
    };

    std::vector<Line> lines_;
    std::unordered_map<std::string, std::size_t> documentFrequency_;
    std::size_t totalLength_ = 0;
};

} // namespace nesso::core

#endif // CORE_BM25_HPP

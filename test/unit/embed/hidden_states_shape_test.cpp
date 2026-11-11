// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_all.hpp>
#include <embed/embed_types.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

using nesso::embed::matchesMiniLmHiddenStatesShape;
using nesso::embed::MINILM_EMBEDDING_DIMENSIONS;

TEST_CASE("matchesMiniLmHiddenStatesShape accepts the MiniLM last-hidden layout", "[embed][Unit][shape]")
{
    constexpr size_t batchSize = 2;
    constexpr size_t sequenceLength = 7;
    const std::array<int64_t, 3> shape{2, 7, static_cast<int64_t>(MINILM_EMBEDDING_DIMENSIONS)};
    REQUIRE(matchesMiniLmHiddenStatesShape(shape, batchSize, sequenceLength));
}

TEST_CASE("matchesMiniLmHiddenStatesShape rejects a wrong batch axis", "[embed][Unit][shape]")
{
    const std::array<int64_t, 3> shape{3, 7, static_cast<int64_t>(MINILM_EMBEDDING_DIMENSIONS)};
    REQUIRE_FALSE(matchesMiniLmHiddenStatesShape(shape, 2, 7));
}

TEST_CASE("matchesMiniLmHiddenStatesShape rejects a wrong sequence axis", "[embed][Unit][shape]")
{
    const std::array<int64_t, 3> shape{2, 8, static_cast<int64_t>(MINILM_EMBEDDING_DIMENSIONS)};
    REQUIRE_FALSE(matchesMiniLmHiddenStatesShape(shape, 2, 7));
}

TEST_CASE("matchesMiniLmHiddenStatesShape rejects a wrong rank", "[embed][Unit][shape]")
{
    const std::array<int64_t, 2> shape{2, static_cast<int64_t>(MINILM_EMBEDDING_DIMENSIONS)};
    REQUIRE_FALSE(matchesMiniLmHiddenStatesShape(shape, 2, 7));
    const std::array<int64_t, 4> tooRanked{2, 7, static_cast<int64_t>(MINILM_EMBEDDING_DIMENSIONS), 1};
    REQUIRE_FALSE(matchesMiniLmHiddenStatesShape(tooRanked, 2, 7));
    REQUIRE_FALSE(matchesMiniLmHiddenStatesShape(std::span<const int64_t>{}, 2, 7));
}

TEST_CASE("matchesMiniLmHiddenStatesShape rejects a wrong hidden size", "[embed][Unit][shape]")
{
    const std::array<int64_t, 3> shape{2, 7, 128};
    REQUIRE_FALSE(matchesMiniLmHiddenStatesShape(shape, 2, 7));
}

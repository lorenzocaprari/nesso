// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_all.hpp>
#include <embed/embed_types.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

using nesso::embed::MatchesMiniLmHiddenStatesShape;
using nesso::embed::MINILM_EMBEDDING_DIMENSIONS;

TEST_CASE("MatchesMiniLmHiddenStatesShape accepts the MiniLM last-hidden layout", "[embed][Unit][shape]")
{
    constexpr size_t batch_size = 2;
    constexpr size_t sequence_length = 7;
    const std::array<int64_t, 3> shape{2, 7, static_cast<int64_t>(MINILM_EMBEDDING_DIMENSIONS)};
    REQUIRE(MatchesMiniLmHiddenStatesShape(shape, batch_size, sequence_length));
}

TEST_CASE("MatchesMiniLmHiddenStatesShape rejects a wrong batch axis", "[embed][Unit][shape]")
{
    const std::array<int64_t, 3> shape{3, 7, static_cast<int64_t>(MINILM_EMBEDDING_DIMENSIONS)};
    REQUIRE_FALSE(MatchesMiniLmHiddenStatesShape(shape, 2, 7));
}

TEST_CASE("MatchesMiniLmHiddenStatesShape rejects a wrong sequence axis", "[embed][Unit][shape]")
{
    const std::array<int64_t, 3> shape{2, 8, static_cast<int64_t>(MINILM_EMBEDDING_DIMENSIONS)};
    REQUIRE_FALSE(MatchesMiniLmHiddenStatesShape(shape, 2, 7));
}

TEST_CASE("MatchesMiniLmHiddenStatesShape rejects a wrong rank", "[embed][Unit][shape]")
{
    const std::array<int64_t, 2> shape{2, static_cast<int64_t>(MINILM_EMBEDDING_DIMENSIONS)};
    REQUIRE_FALSE(MatchesMiniLmHiddenStatesShape(shape, 2, 7));
    const std::array<int64_t, 4> too_ranked{2, 7, static_cast<int64_t>(MINILM_EMBEDDING_DIMENSIONS), 1};
    REQUIRE_FALSE(MatchesMiniLmHiddenStatesShape(too_ranked, 2, 7));
    REQUIRE_FALSE(MatchesMiniLmHiddenStatesShape(std::span<const int64_t>{}, 2, 7));
}

TEST_CASE("MatchesMiniLmHiddenStatesShape rejects a wrong hidden size", "[embed][Unit][shape]")
{
    const std::array<int64_t, 3> shape{2, 7, 128};
    REQUIRE_FALSE(MatchesMiniLmHiddenStatesShape(shape, 2, 7));
}

// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#ifndef NESSO_TEST_EMBEDDER_IDENTITY_HPP
#define NESSO_TEST_EMBEDDER_IDENTITY_HPP

#include <embed/embedder.hpp>

#include <string>

inline nesso::embed::EmbedderIdentity testEmbedderIdentity()
{
    return {.modelId = "test/model",
            .modelSha256 = std::string(64, 'a'),
            .vocabSha256 = std::string(64, 'b'),
            .quantized = true,
            .dimensions = 4};
}

#endif // NESSO_TEST_EMBEDDER_IDENTITY_HPP

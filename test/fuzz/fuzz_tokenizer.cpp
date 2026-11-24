// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0
// Licensed under the Apache License, Version 2.0. See LICENSE for details.

#include <embed/wordpiece_tokenizer.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <unistd.h>

namespace
{

constexpr std::size_t MAX_ENCODE_BYTES = 65536;

auto BytesFrom(const std::uint8_t *data, std::size_t size) -> std::string
{
    if (data == nullptr || size == 0)
    {
        return {};
    }
    return std::string(reinterpret_cast<const char *>(data), size);
}

} // namespace

extern "C" auto LLVMFuzzerTestOneInput(const std::uint8_t *data, std::size_t size) -> int
{
    const std::string bytes = BytesFrom(data, size);
    const auto input_path =
        std::filesystem::temp_directory_path() / ("nesso-fuzz-input-" + std::to_string(::getpid()) + ".txt");
    {
        std::ofstream out(input_path, std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }

    if (auto loaded = nesso::embed::WordPieceTokenizer::FromVocabFile(input_path))
    {
        static_cast<void>(loaded->Encode("the quick brown fox"));
    }

    const auto vocab_path =
        std::filesystem::temp_directory_path() / ("nesso-fuzz-vocab-" + std::to_string(::getpid()) + ".txt");
    {
        std::ofstream vocab(vocab_path, std::ios::trunc);
        vocab << "[CLS]\n[SEP]\n[UNK]\nhello\nworld\n##ing\n";
    }

    if (auto tok = nesso::embed::WordPieceTokenizer::FromVocabFile(vocab_path))
    {
        std::string capped = bytes;
        if (capped.size() > MAX_ENCODE_BYTES)
        {
            capped.resize(MAX_ENCODE_BYTES);
        }
        static_cast<void>(tok->Encode(capped));
    }

    std::error_code ec;
    std::filesystem::remove(input_path, ec);
    std::filesystem::remove(vocab_path, ec);
    return 0;
}

// libFuzzer supplies main. The AFL++ CI job keeps this file-input entry point.
#ifndef NESSO_LIBFUZZER
auto main(int argc, char *argv[]) -> int
{
    if (argc < 2)
    {
        return 1;
    }

    std::ifstream in(argv[1], std::ios::binary);
    if (!in)
    {
        return 0;
    }
    const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return LLVMFuzzerTestOneInput(reinterpret_cast<const std::uint8_t *>(bytes.data()), bytes.size());
}
#endif

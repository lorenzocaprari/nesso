// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT
// Licensed under the MIT License. See LICENSE for details.

#include <embed/wordpiece_tokenizer.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <unistd.h>

namespace
{

constexpr std::size_t kMaxEncodeBytes = 65536;

std::string bytesFrom(const std::uint8_t *data, std::size_t size)
{
    if (data == nullptr || size == 0)
    {
        return {};
    }
    return std::string(reinterpret_cast<const char *>(data), size);
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data, std::size_t size)
{
    const std::string bytes = bytesFrom(data, size);
    const auto inputPath =
        std::filesystem::temp_directory_path() / ("nesso-fuzz-input-" + std::to_string(::getpid()) + ".txt");
    {
        std::ofstream out(inputPath, std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }

    if (auto loaded = embed::WordPieceTokenizer::fromVocabFile(inputPath))
    {
        (void)loaded->encode("the quick brown fox");
    }

    const auto vocabPath =
        std::filesystem::temp_directory_path() / ("nesso-fuzz-vocab-" + std::to_string(::getpid()) + ".txt");
    {
        std::ofstream vocab(vocabPath, std::ios::trunc);
        vocab << "[CLS]\n[SEP]\n[UNK]\nhello\nworld\n##ing\n";
    }

    if (auto tok = embed::WordPieceTokenizer::fromVocabFile(vocabPath))
    {
        std::string capped = bytes;
        if (capped.size() > kMaxEncodeBytes)
        {
            capped.resize(kMaxEncodeBytes);
        }
        (void)tok->encode(capped);
    }

    std::error_code ec;
    std::filesystem::remove(inputPath, ec);
    std::filesystem::remove(vocabPath, ec);
    return 0;
}

// libFuzzer supplies main. The AFL++ CI job keeps this file-input entry point.
#ifndef NESSO_LIBFUZZER
int main(int argc, char *argv[])
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

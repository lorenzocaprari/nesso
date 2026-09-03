// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT
// Licensed under the MIT License. See LICENSE for details.

#include <embed/wordpiece_tokenizer.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <unistd.h>

namespace
{

constexpr size_t kMaxEncodeBytes = 65536;

std::string readCapped(const char *path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        return {};
    }
    std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (bytes.size() > kMaxEncodeBytes)
    {
        bytes.resize(kMaxEncodeBytes);
    }
    return bytes;
}

} // namespace

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        return 1;
    }

    if (auto loaded = embed::WordPieceTokenizer::fromVocabFile(argv[1]))
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
        (void)tok->encode(readCapped(argv[1]));
    }

    std::error_code ec;
    std::filesystem::remove(vocabPath, ec);
    return 0;
}

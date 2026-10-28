// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0
// Licensed under the Apache License, Version 2.0. See LICENSE for details.

#include <parser/log_chunker.hpp>
#include <parser/template_mask.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <unistd.h>

namespace
{

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
    volatile std::size_t sink = nesso::parser::maskTemplate(bytes).size();

    const auto dir = std::filesystem::temp_directory_path() / ("nesso-fuzz-parser-" + std::to_string(::getpid()));
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    for (const char *ext : {".log", ".json", ".jsonl"})
    {
        const std::filesystem::path path = dir / (std::string("input") + ext);
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        out.close();
        const auto chunks = nesso::parser::Chunker::fromFile(path);
        if (chunks.has_value())
        {
            for (const nesso::parser::ParsedChunk &chunk : *chunks)
            {
                sink += nesso::parser::maskTemplate(chunk.text).size();
            }
        }
    }

    std::filesystem::remove_all(dir, ec);
    return sink == static_cast<std::size_t>(-1) && bytes.empty() ? 1 : 0;
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

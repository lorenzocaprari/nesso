// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT
// Licensed under the MIT License. See LICENSE for details.

#include <parser/log_chunker.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

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

    const auto dir = std::filesystem::temp_directory_path() / ("nesso-fuzz-parser-" + std::to_string(::getpid()));
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    for (const char *ext : {".log", ".json", ".jsonl"})
    {
        const std::filesystem::path path = dir / (std::string("input") + ext);
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        out.close();
        (void)parser::LogChunker::fromFile(path);
    }

    std::filesystem::remove_all(dir, ec);
    return 0;
}

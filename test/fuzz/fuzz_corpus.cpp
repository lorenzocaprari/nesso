// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0
// Licensed under the Apache License, Version 2.0. See LICENSE for details.

#include <core/corpus_index.hpp>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <unistd.h>

namespace
{

std::string BytesFrom(const std::uint8_t *data, std::size_t size)
{
    if (data == nullptr || size == 0)
    {
        return {};
    }
    return std::string(reinterpret_cast<const char *>(data), size);
}

// MapCorpusFile rejects a checksum mismatch before it parses the body.
// Rewrite the field from the fuzzed bytes so mutations still reach that
// parser. The comparison in the reader is unchanged.
void SealCorpusChecksum(std::string &bytes)
{
    constexpr std::size_t checksum_offset = nesso::core::CORPUS_CHECKSUM_OFFSET;
    constexpr std::size_t checksum_bytes = nesso::core::CORPUS_CHECKSUM_BYTES;
    if (bytes.size() < checksum_offset + checksum_bytes)
    {
        return;
    }
    const nesso::core::CorpusChecksum checksum = nesso::core::ComputeCorpusChecksum(std::as_bytes(std::span{bytes}));
    std::memcpy(bytes.data() + checksum_offset, checksum.data(), checksum.size());
}

std::size_t TouchMappedCorpus(const nesso::core::MappedCorpus &corpus)
{
    std::size_t sink = corpus.Dimensions();
    sink += corpus.Policy().size();
    sink += corpus.Sources().size();
    sink += corpus.Lines().size();
    sink += corpus.Texts().size();
    sink += corpus.Matrix().size();
    sink += corpus.LexicalDictionary().size();
    sink += corpus.LexicalPostings().size();
    sink += corpus.CorpusLexicalTermsVersion().size();
    sink += static_cast<std::size_t>(corpus.GetLexicalStatus());
    for (const nesso::core::SourceInfo &source : corpus.Sources())
    {
        sink += source.path.size();
        sink += source.json_field.size();
        sink += static_cast<std::size_t>(source.size);
        sink += source.tracked ? 1U : 0U;
        sink += source.stored_only ? 1U : 0U;
    }
    for (const nesso::core::CorpusLine &line : corpus.Lines())
    {
        sink += static_cast<std::size_t>(line.line_number);
        sink += static_cast<std::size_t>(line.byte_offset);
        sink += static_cast<std::size_t>(line.byte_length);
        sink += line.source_index;
        sink += line.distinct_id;
        sink += line.array_element ? 1U : 0U;
        if (line.stored_text.has_value())
        {
            sink += line.stored_text->size();
        }
    }
    for (const std::string_view text : corpus.Texts())
    {
        sink += text.size();
    }
    for (const float value : corpus.Matrix())
    {
        sink += value == 0.F ? 0U : 1U;
    }
    for (const std::string_view term : corpus.LexicalDictionary())
    {
        sink += term.size();
    }
    for (const nesso::core::LexicalLinePostings &postings : corpus.LexicalPostings())
    {
        sink += postings.length;
        sink += postings.entries.size();
    }
    return sink;
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data, std::size_t size)
{
    std::string bytes = BytesFrom(data, size);
    SealCorpusChecksum(bytes);
    volatile std::size_t sink = bytes.size();

    const auto dir = std::filesystem::temp_directory_path() / ("nesso-fuzz-corpus-" + std::to_string(::getpid()));
    std::error_code error;
    std::filesystem::create_directories(dir, error);
    const std::filesystem::path path = dir / "input.nesso";
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }

    {
        std::uint32_t observed_version = 0;
        auto mapped = nesso::core::MapCorpusFile(path, &observed_version);
        sink += observed_version;
        if (mapped.has_value())
        {
            sink += TouchMappedCorpus(*mapped);
        }
        else
        {
            sink += static_cast<std::size_t>(mapped.error());
        }
    }

    std::filesystem::remove_all(dir, error);
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

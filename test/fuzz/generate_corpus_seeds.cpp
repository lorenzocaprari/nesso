// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0
// Licensed under the Apache License, Version 2.0. See LICENSE for details.

// Writes the small corpus seeds consumed by nesso_fuzz_corpus.
//   nesso_generate_corpus_seeds test/fuzz/seeds/corpus

#include <core/bm25.hpp>
#include <core/corpus_index.hpp>
#include <core/lexical_terms.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace
{

using nesso::core::CorpusLine;
using nesso::core::CorpusPolicy;
using nesso::core::CorpusWriter;
using nesso::core::NO_DISTINCT;
using nesso::core::SourceInfo;

CorpusPolicy Policy(std::uint32_t dimensions)
{
    return {{"batch", "window:1024,max:32,tokens:1024,order:length-then-input"},
            {"bm25", std::string{nesso::core::BM25_POLICY}},
            {"dedup", "masked-v1"},
            {"dimensions", std::to_string(dimensions)},
            {"lexical.terms", std::string{nesso::core::LEXICAL_TERMS_VERSION}},
            {"mask", "template-v1"},
            {"model.id", "model"},
            {"model.quantization", "int8"},
            {"model.sha256", std::string(64, 'a')},
            {"tokens.max", "256"},
            {"truncate", "4096-codepoints"},
            {"vocab.sha256", std::string(64, 'b')}};
}

bool Report(const std::filesystem::path &path)
{
    std::cerr << "failed to write " << path << '\n';
    return false;
}

bool WriteEmpty(const std::filesystem::path &path)
{
    auto writer = CorpusWriter::Create(path, Policy(0));
    if (!writer || !writer->Finish())
    {
        return Report(path);
    }
    return true;
}

bool WriteLine(const std::filesystem::path &path)
{
    auto writer = CorpusWriter::Create(path, Policy(2));
    if (!writer)
    {
        return Report(path);
    }
    const SourceInfo source{
        .path = "app.log", .size = 16, .modified_time = 1'700'000'000, .tracked = true, .json_field = "message"};
    const std::array<float, 2> embedding{0.25F, -0.5F};
    const std::array<CorpusLine, 1> lines{
        CorpusLine{.line_number = 1, .byte_offset = 0, .byte_length = 16, .source_index = 0, .distinct_id = 0}};
    const std::array<std::vector<std::string>, 1> terms{nesso::core::LexicalTerms("payment timeout")};
    if (writer->AddSource(source) != 0 || writer->AddDistinct("payment timeout", embedding) != 0 ||
        !writer->AddLines(lines, terms) || !writer->Finish())
    {
        return Report(path);
    }
    return true;
}

bool WriteStored(const std::filesystem::path &path)
{
    auto writer = CorpusWriter::Create(path, Policy(2));
    if (!writer)
    {
        return Report(path);
    }
    const SourceInfo stdin_source{.path = "-", .stored_only = true};
    const SourceInfo archive_source{.path = "bundle.tar!app.log", .stored_only = true};
    const std::array<float, 2> refused{1.F, 0.F};
    const std::array<float, 2> disk{0.F, 1.F};
    const std::array<CorpusLine, 3> lines{
        CorpusLine{.line_number = 1, .source_index = 0, .distinct_id = 0, .stored_text = "connection refused"},
        CorpusLine{.line_number = 2, .source_index = 0, .distinct_id = NO_DISTINCT, .stored_text = ""},
        CorpusLine{
            .line_number = 1, .source_index = 1, .distinct_id = 1, .array_element = true, .stored_text = "disk full"}};
    const std::array<std::vector<std::string>, 3> terms{
        nesso::core::LexicalTerms("connection refused"), {}, nesso::core::LexicalTerms("disk full")};
    if (writer->AddSource(stdin_source) != 0 || writer->AddSource(archive_source) != 1 ||
        writer->AddDistinct("connection refused", refused) != 0 || writer->AddDistinct("disk full", disk) != 1 ||
        !writer->AddLines(lines, terms) || !writer->Finish())
    {
        return Report(path);
    }
    return true;
}

} // namespace

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        std::cerr << "usage: nesso_generate_corpus_seeds <directory>\n";
        return 1;
    }
    const std::filesystem::path dest{argv[1]};
    std::error_code error;
    std::filesystem::create_directories(dest, error);
    if (error || !WriteEmpty(dest / "empty.nesso") || !WriteLine(dest / "line.nesso") ||
        !WriteStored(dest / "stored.nesso"))
    {
        return 1;
    }
    return 0;
}

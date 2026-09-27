// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#ifndef NESSO_CORPUS_COMMANDS_HPP
#define NESSO_CORPUS_COMMANDS_HPP

#include <cstddef>
#include <filesystem>
#include <span>
#include <string_view>

namespace nesso::commands
{

int runCorpusIndex(std::span<const std::filesystem::path> files, const std::filesystem::path &output,
                   const std::filesystem::path &modelDir);

int runCorpusSearch(std::string_view query, const std::filesystem::path &indexPath, size_t topK,
                    const std::filesystem::path &modelDir);

} // namespace nesso::commands

#endif // NESSO_CORPUS_COMMANDS_HPP

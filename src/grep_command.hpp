// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#ifndef NESSO_GREP_COMMAND_HPP
#define NESSO_GREP_COMMAND_HPP

#include <cstddef>
#include <filesystem>
#include <string>

namespace nesso::commands
{

int runGrep(const std::filesystem::path &logPath, const std::string &query, size_t topK,
            const std::filesystem::path &modelDir);

} // namespace nesso::commands

#endif // NESSO_GREP_COMMAND_HPP

// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#ifndef NESSO_GREP_COMMAND_HPP
#define NESSO_GREP_COMMAND_HPP

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace nesso::commands
{

int runGrep(std::string_view query, std::span<const std::filesystem::path> files, size_t topK,
            const std::filesystem::path &modelDir);

} // namespace nesso::commands

#endif // NESSO_GREP_COMMAND_HPP

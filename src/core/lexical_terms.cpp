// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <core/lexical_terms.hpp>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace nesso::core
{

static bool IsAsciiUpper(unsigned char byte) noexcept { return byte >= 'A' && byte <= 'Z'; }

static bool IsAsciiLower(unsigned char byte) noexcept { return byte >= 'a' && byte <= 'z'; }

static bool IsAsciiDigit(unsigned char byte) noexcept { return byte >= '0' && byte <= '9'; }

static bool IsTermByte(unsigned char byte) noexcept
{
    return IsAsciiUpper(byte) || IsAsciiLower(byte) || IsAsciiDigit(byte) || byte >= 0x80U;
}

static bool IsJoiner(unsigned char byte) noexcept { return byte == '-' || byte == '_' || byte == '.' || byte == ':'; }

static char AsciiLower(unsigned char byte) noexcept
{
    if (IsAsciiUpper(byte))
    {
        return static_cast<char>(byte - 'A' + 'a');
    }
    return static_cast<char>(byte);
}

static std::string Lowered(std::string_view text, std::size_t begin, std::size_t end)
{
    std::string out(end - begin, '\0');
    for (std::size_t index = begin; index < end; ++index)
    {
        out[index - begin] = AsciiLower(static_cast<unsigned char>(text[index]));
    }
    return out;
}

static void AppendRun(std::vector<std::string> &terms, std::string_view text, std::size_t begin, std::size_t end)
{
    std::size_t piece_begin = begin;
    for (std::size_t index = begin + 1; index < end; ++index)
    {
        const auto current = static_cast<unsigned char>(text[index]);
        const auto previous = static_cast<unsigned char>(text[index - 1]);
        // Lowercase then uppercase is the camelCase hump. Acronyms stay intact:
        // splitting them would turn `IPv4` into `i` and `pv4`.
        if (IsAsciiUpper(current) && IsAsciiLower(previous))
        {
            terms.push_back(Lowered(text, piece_begin, index));
            piece_begin = index;
        }
    }
    terms.push_back(Lowered(text, piece_begin, end));
}

std::vector<std::string> LexicalTerms(std::string_view text)
{
    std::vector<std::string> terms;
    const std::size_t size = text.size();
    std::size_t index = 0;
    while (index < size)
    {
        if (!IsTermByte(static_cast<unsigned char>(text[index])))
        {
            ++index;
            continue;
        }

        const std::size_t group_begin = index;
        std::size_t runs = 0;
        while (index < size && IsTermByte(static_cast<unsigned char>(text[index])))
        {
            const std::size_t run_begin = index;
            while (index < size && IsTermByte(static_cast<unsigned char>(text[index])))
            {
                ++index;
            }
            AppendRun(terms, text, run_begin, index);
            ++runs;

            std::size_t after_joiners = index;
            while (after_joiners < size && IsJoiner(static_cast<unsigned char>(text[after_joiners])))
            {
                ++after_joiners;
            }
            if (after_joiners > index && after_joiners < size &&
                IsTermByte(static_cast<unsigned char>(text[after_joiners])))
            {
                index = after_joiners;
                continue;
            }
            break;
        }

        if (runs >= 2)
        {
            terms.push_back(Lowered(text, group_begin, index));
        }
    }
    return terms;
}

bool TermByteLess(std::string_view left, std::string_view right) noexcept
{
    const std::size_t count = left.size() < right.size() ? left.size() : right.size();
    for (std::size_t index = 0; index < count; ++index)
    {
        const auto left_byte = static_cast<unsigned char>(left[index]);
        const auto right_byte = static_cast<unsigned char>(right[index]);
        if (left_byte != right_byte)
        {
            return left_byte < right_byte;
        }
    }
    return left.size() < right.size();
}

} // namespace nesso::core

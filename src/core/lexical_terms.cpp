// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <core/lexical_terms.hpp>

namespace nesso::core
{

static bool isAsciiUpper(unsigned char byte) noexcept { return byte >= 'A' && byte <= 'Z'; }

static bool isAsciiLower(unsigned char byte) noexcept { return byte >= 'a' && byte <= 'z'; }

static bool isAsciiDigit(unsigned char byte) noexcept { return byte >= '0' && byte <= '9'; }

static bool isTermByte(unsigned char byte) noexcept
{
    return isAsciiUpper(byte) || isAsciiLower(byte) || isAsciiDigit(byte) || byte >= 0x80U;
}

static bool isJoiner(unsigned char byte) noexcept { return byte == '-' || byte == '_' || byte == '.' || byte == ':'; }

static char asciiLower(unsigned char byte) noexcept
{
    if (isAsciiUpper(byte))
    {
        return static_cast<char>(byte - 'A' + 'a');
    }
    return static_cast<char>(byte);
}

static std::string lowered(std::string_view text, std::size_t begin, std::size_t end)
{
    std::string out(end - begin, '\0');
    for (std::size_t index = begin; index < end; ++index)
    {
        out[index - begin] = asciiLower(static_cast<unsigned char>(text[index]));
    }
    return out;
}

static void appendRun(std::vector<std::string> &terms, std::string_view text, std::size_t begin, std::size_t end)
{
    std::size_t pieceBegin = begin;
    for (std::size_t index = begin + 1; index < end; ++index)
    {
        const auto current = static_cast<unsigned char>(text[index]);
        const auto previous = static_cast<unsigned char>(text[index - 1]);
        // Lowercase then uppercase is the camelCase hump. Acronyms stay intact:
        // splitting them would turn `IPv4` into `i` and `pv4`.
        if (isAsciiUpper(current) && isAsciiLower(previous))
        {
            terms.push_back(lowered(text, pieceBegin, index));
            pieceBegin = index;
        }
    }
    terms.push_back(lowered(text, pieceBegin, end));
}

std::vector<std::string> lexicalTerms(std::string_view text)
{
    std::vector<std::string> terms;
    const std::size_t size = text.size();
    std::size_t index = 0;
    while (index < size)
    {
        if (!isTermByte(static_cast<unsigned char>(text[index])))
        {
            ++index;
            continue;
        }

        const std::size_t groupBegin = index;
        std::size_t runs = 0;
        while (index < size && isTermByte(static_cast<unsigned char>(text[index])))
        {
            const std::size_t runBegin = index;
            while (index < size && isTermByte(static_cast<unsigned char>(text[index])))
            {
                ++index;
            }
            appendRun(terms, text, runBegin, index);
            ++runs;

            std::size_t afterJoiners = index;
            while (afterJoiners < size && isJoiner(static_cast<unsigned char>(text[afterJoiners])))
            {
                ++afterJoiners;
            }
            if (afterJoiners > index && afterJoiners < size &&
                isTermByte(static_cast<unsigned char>(text[afterJoiners])))
            {
                index = afterJoiners;
                continue;
            }
            break;
        }

        if (runs >= 2)
        {
            terms.push_back(lowered(text, groupBegin, index));
        }
    }
    return terms;
}

} // namespace nesso::core

// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <core/corpus_index.hpp>
#include <core/sha256.hpp>

#include <platform/file.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <system_error>
#include <unistd.h>
#include <unordered_set>
#include <utility>

namespace nesso::core
{

static_assert(std::endian::native == std::endian::little);

constexpr std::uint64_t HEADER_BYTES = 128;
constexpr std::uint64_t SOURCE_RECORD_BYTES = 48;
constexpr std::uint64_t LINE_RECORD_BYTES = 56;
constexpr std::uint64_t TEXT_RECORD_BYTES = 16;
constexpr std::uint64_t MAX_POLICY_BYTES = 64ULL * 1024ULL;
constexpr std::uint64_t MAX_TEXT_BYTES = 8ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t MAX_SOURCES = 1'000'000;
constexpr std::uint64_t MAX_LINES = 10'000'000;
constexpr std::uint64_t MAX_DISTINCT = 1'000'000;
constexpr std::uint32_t MAX_DIMENSIONS = 65536;
constexpr std::array<char, 4> MAGIC{'N', 'E', 'S', 'C'};
constexpr std::uint32_t SOURCE_TRACKED = 1U;
constexpr std::uint32_t SOURCE_STORED_ONLY = 2U;
constexpr std::uint32_t LINE_ARRAY_ELEMENT = 1U;
constexpr std::uint32_t LINE_STORED_TEXT = 2U;
constexpr std::uint32_t LINE_SKIPPED = 4U;

struct Layout
{
    std::uint32_t policyBytes = 0;
    std::uint32_t dimensions = 0;
    std::uint64_t sourceCount = 0;
    std::uint64_t lineCount = 0;
    std::uint64_t sourceOffset = 0;
    std::uint64_t lineOffset = 0;
    std::uint64_t matrixOffset = 0;
    std::uint64_t distinctCount = 0;
    std::uint64_t distinctOffset = 0;
    std::uint64_t textOffset = 0;
    std::uint64_t storedOffset = 0;
    std::uint64_t lexicalOffset = 0;
    std::uint64_t lexicalBytes = 0;
    std::uint64_t policyOffset = 0;
};

struct SpillFile
{
    std::filesystem::path path;
    std::ofstream output;
};

struct SpilledLine
{
    std::uint64_t lineNumber = 0;
    std::uint64_t byteOffset = 0;
    std::uint64_t byteLength = 0;
    std::uint32_t sourceIndex = 0;
    std::uint32_t distinctId = NO_DISTINCT;
    std::uint32_t flags = 0;
    std::string storedText;
};

static bool rangeInside(std::uint64_t offset, std::uint64_t length, std::uint64_t limit)
{
    return offset <= limit && length <= limit - offset;
}

static std::uint64_t alignUp(std::uint64_t value, std::uint64_t alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

static bool writeRaw(std::ostream &output, const void *data, std::size_t size)
{
    if (size == 0)
    {
        return true;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    output.write(reinterpret_cast<const char *>(data), static_cast<std::streamsize>(size));
    return static_cast<bool>(output);
}

static bool writeU32(std::ostream &output, std::uint32_t value)
{
    const std::array<unsigned char, 4> bytes{static_cast<unsigned char>(value), static_cast<unsigned char>(value >> 8U),
                                             static_cast<unsigned char>(value >> 16U),
                                             static_cast<unsigned char>(value >> 24U)};
    return writeRaw(output, bytes.data(), bytes.size());
}

static bool writeU64(std::ostream &output, std::uint64_t value)
{
    const std::array<unsigned char, 8> bytes{
        static_cast<unsigned char>(value),        static_cast<unsigned char>(value >> 8U),
        static_cast<unsigned char>(value >> 16U), static_cast<unsigned char>(value >> 24U),
        static_cast<unsigned char>(value >> 32U), static_cast<unsigned char>(value >> 40U),
        static_cast<unsigned char>(value >> 48U), static_cast<unsigned char>(value >> 56U)};
    return writeRaw(output, bytes.data(), bytes.size());
}

static bool readExact(std::istream &input, void *data, std::size_t size)
{
    if (size == 0)
    {
        return true;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    input.read(reinterpret_cast<char *>(data), static_cast<std::streamsize>(size));
    return std::cmp_equal(input.gcount(), size);
}

static bool readU32Stream(std::istream &input, std::uint32_t &value)
{
    std::array<unsigned char, 4> bytes{};
    if (!readExact(input, bytes.data(), bytes.size()))
    {
        return false;
    }
    value = static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8U) |
            (static_cast<std::uint32_t>(bytes[2]) << 16U) | (static_cast<std::uint32_t>(bytes[3]) << 24U);
    return true;
}

static bool readU64Stream(std::istream &input, std::uint64_t &value)
{
    std::uint32_t low = 0;
    std::uint32_t high = 0;
    if (!readU32Stream(input, low) || !readU32Stream(input, high))
    {
        return false;
    }
    value = static_cast<std::uint64_t>(low) | (static_cast<std::uint64_t>(high) << 32U);
    return true;
}

static bool readU32(std::span<const std::byte> bytes, std::uint64_t offset, std::uint32_t &value)
{
    if (!rangeInside(offset, sizeof(value), bytes.size()))
    {
        return false;
    }
    value = 0;
    for (unsigned shift = 0; shift < 32; shift += 8)
    {
        value |= std::to_integer<std::uint32_t>(bytes[offset++]) << shift;
    }
    return true;
}

static bool readU64(std::span<const std::byte> bytes, std::uint64_t offset, std::uint64_t &value)
{
    if (!rangeInside(offset, sizeof(value), bytes.size()))
    {
        return false;
    }
    value = 0;
    for (unsigned shift = 0; shift < 64; shift += 8)
    {
        value |= std::to_integer<std::uint64_t>(bytes[offset++]) << shift;
    }
    return true;
}

static std::string_view viewBytes(std::span<const std::byte> bytes, std::uint64_t offset, std::uint64_t length)
{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return {reinterpret_cast<const char *>(bytes.data() + offset), static_cast<std::size_t>(length)};
}

static bool validKey(std::string_view key)
{
    return !key.empty() && std::ranges::all_of(key,
                                               [](char character)
                                               {
                                                   return (character >= 'a' && character <= 'z') ||
                                                          (character >= '0' && character <= '9') || character == '.' ||
                                                          character == '-' || character == '_';
                                               });
}

static bool validHash(std::string_view value)
{
    return value.size() == 64 &&
           std::ranges::all_of(
               value, [](char character)
               { return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f'); });
}

static bool parseNumber(std::string_view text, std::uint32_t &value)
{
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() && std::to_string(value) == text;
}

static std::expected<std::uint32_t, EngineError> validatePolicy(const CorpusPolicy &policy)
{
    constexpr std::array<std::string_view, 10> required{
        "batch",        "dedup",      "dimensions", "mask",        "model.id", "model.quantization",
        "model.sha256", "tokens.max", "truncate",   "vocab.sha256"};
    for (const std::string_view key : required)
    {
        if (!policy.contains(key))
        {
            return std::unexpected(EngineError::CorruptDatabase);
        }
    }
    std::uint64_t size = 0;
    for (const auto &[key, value] : policy)
    {
        if (!validKey(key) || value.empty() || value.find_first_of("\r\n\0", 0, 3) != std::string::npos)
        {
            return std::unexpected(EngineError::CorruptDatabase);
        }
        size += key.size() + value.size() + 2;
        if (size > MAX_POLICY_BYTES)
        {
            return std::unexpected(EngineError::CorruptDatabase);
        }
    }
    std::uint32_t dimensions = 0;
    std::uint32_t maxTokens = 0;
    const auto &quantization = policy.at("model.quantization");
    if (!parseNumber(policy.at("dimensions"), dimensions) || dimensions > MAX_DIMENSIONS ||
        !parseNumber(policy.at("tokens.max"), maxTokens) || maxTokens == 0 || policy.at("model.id").size() > 1024 ||
        !validHash(policy.at("model.sha256")) || !validHash(policy.at("vocab.sha256")) ||
        (quantization != "int8" && quantization != "fp32"))
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    return dimensions;
}

static std::expected<void, EngineError> validateVector(std::span<const float> embedding, std::uint32_t dimensions)
{
    if (embedding.empty())
    {
        return std::unexpected(EngineError::DatabaseNotInitialized);
    }
    if (embedding.size() != dimensions)
    {
        return std::unexpected(EngineError::MismatchedDimensions);
    }
    if (!std::ranges::all_of(embedding, [](float value) { return std::isfinite(value); }))
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    return {};
}

static std::string serializePolicy(const CorpusPolicy &policy)
{
    std::string text;
    for (const auto &[key, value] : policy)
    {
        text += key;
        text += '=';
        text += value;
        text += '\n';
    }
    return text;
}

static std::expected<CorpusPolicy, EngineError> parsePolicy(std::string_view text)
{
    CorpusPolicy policy;
    std::string previous;
    while (!text.empty())
    {
        const auto newline = text.find('\n');
        if (newline == std::string_view::npos)
        {
            return std::unexpected(EngineError::CorruptDatabase);
        }
        const std::string_view record = text.substr(0, newline);
        const auto separator = record.find('=');
        if (separator == std::string_view::npos)
        {
            return std::unexpected(EngineError::CorruptDatabase);
        }
        std::string key{record.substr(0, separator)};
        if (!previous.empty() && key <= previous)
        {
            return std::unexpected(EngineError::CorruptDatabase);
        }
        previous = key;
        policy.emplace(std::move(key), std::string{record.substr(separator + 1)});
        text.remove_prefix(newline + 1);
    }
    if (!validatePolicy(policy))
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    return policy;
}

static std::expected<SpillFile, EngineError> createSpill(const std::filesystem::path &destination,
                                                         std::string_view suffix)
{
    static std::atomic<std::uint64_t> counter{0};
    for (unsigned attempt = 0; attempt < 16; ++attempt)
    {
        const auto number = counter.fetch_add(1, std::memory_order_relaxed);
        const std::filesystem::path path = destination.string() + "." + std::string{suffix} + "." +
                                           std::to_string(getpid()) + "." + std::to_string(number);
        errno = 0;
        std::ofstream output(path, std::ios::binary | std::ios::out | std::ios::noreplace);
        if (output)
        {
            return SpillFile{.path = path, .output = std::move(output)};
        }
        if (errno != EEXIST)
        {
            return std::unexpected(EngineError::FileOpenFailure);
        }
    }
    return std::unexpected(EngineError::FileOpenFailure);
}

static void removeFile(const std::filesystem::path &path)
{
    if (!path.empty())
    {
        std::error_code error;
        std::filesystem::remove(path, error);
    }
}

static bool readSpilledLine(std::istream &input, SpilledLine &line)
{
    std::uint32_t length = 0;
    if (!readU64Stream(input, line.lineNumber) || !readU64Stream(input, line.byteOffset) ||
        !readU64Stream(input, line.byteLength) || !readU32Stream(input, line.sourceIndex) ||
        !readU32Stream(input, line.distinctId) || !readU32Stream(input, line.flags) || !readU32Stream(input, length) ||
        length > MAX_TEXT_BYTES)
    {
        return false;
    }
    line.storedText.assign(length, '\0');
    return readExact(input, line.storedText.data(), length);
}

static bool readSpilledText(std::istream &input, std::string &text)
{
    std::uint32_t length = 0;
    if (!readU32Stream(input, length) || length > MAX_TEXT_BYTES)
    {
        return false;
    }
    text.assign(length, '\0');
    return readExact(input, text.data(), length);
}

static bool rewind(std::istream &input)
{
    input.clear();
    input.seekg(0);
    return static_cast<bool>(input);
}

static bool atEnd(std::istream &input)
{
    return input.peek() == std::char_traits<char>::eof() && input.eof() && !input.bad();
}

static bool copyBytes(std::istream &input, std::ostream &output, std::uint64_t bytes)
{
    std::array<char, 64ULL * 1024ULL> buffer{};
    while (bytes > 0)
    {
        const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(bytes, buffer.size()));
        if (!readExact(input, buffer.data(), count) || !writeRaw(output, buffer.data(), count))
        {
            return false;
        }
        bytes -= count;
    }
    return atEnd(input);
}

static bool writePadding(std::ostream &output, std::uint64_t count)
{
    constexpr std::array<char, 64> zeros{};
    while (count > 0)
    {
        const auto part = static_cast<std::size_t>(std::min<std::uint64_t>(count, zeros.size()));
        if (!writeRaw(output, zeros.data(), part))
        {
            return false;
        }
        count -= part;
    }
    return true;
}

static bool isZero(std::span<const std::byte> bytes, std::uint64_t begin, std::uint64_t end)
{
    if (begin > end || end > bytes.size())
    {
        return false;
    }
    return std::ranges::all_of(bytes.subspan(begin, end - begin),
                               [](std::byte value) { return value == std::byte{0}; });
}

CorpusChecksum corpusChecksum(std::span<const std::byte> file)
{
    constexpr std::array<std::byte, CORPUS_CHECKSUM_BYTES> zeros{};
    const std::array<std::span<const std::byte>, 3> parts{file.first(CORPUS_CHECKSUM_OFFSET), zeros,
                                                          file.subspan(CORPUS_CHECKSUM_OFFSET + CORPUS_CHECKSUM_BYTES)};
    const Sha256Digest digest = sha256(parts);
    CorpusChecksum checksum{};
    std::ranges::transform(std::span{digest}.first(CORPUS_CHECKSUM_BYTES), checksum.begin(),
                           [](std::uint8_t value) { return std::byte{value}; });
    return checksum;
}

static bool sealCorpus(const std::filesystem::path &path)
{
    CorpusChecksum checksum{};
    {
        auto mapped = nesso::platform::MappedFile::open(path);
        if (!mapped || mapped->bytes().size() < HEADER_BYTES)
        {
            return false;
        }
        checksum = corpusChecksum(mapped->bytes());
    }
    std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
    file.seekp(static_cast<std::streamoff>(CORPUS_CHECKSUM_OFFSET));
    if (!writeRaw(file, checksum.data(), checksum.size()))
    {
        return false;
    }
    file.close();
    return !file.fail();
}

std::expected<SourceInfo, EngineError> sourceInfoFor(const std::filesystem::path &path, std::string_view jsonField)
{
    SourceInfo source{.path = path.string(), .jsonField = std::string{jsonField}};
    if (path == "-")
    {
        source.storedOnly = true;
        return source;
    }
    std::error_code error;
    source.size = std::filesystem::file_size(path, error);
    if (error)
    {
        return std::unexpected(EngineError::FileOpenFailure);
    }
    const auto modified = std::filesystem::last_write_time(path, error);
    if (error)
    {
        return std::unexpected(EngineError::FileOpenFailure);
    }
    source.modifiedTime = std::chrono::duration_cast<std::chrono::nanoseconds>(modified.time_since_epoch()).count();
    source.tracked = true;
    return source;
}

struct CorpusWriter::Impl
{
    std::filesystem::path destination;
    std::filesystem::path temporary;
    CorpusPolicy policy;
    std::uint32_t dimensions = 0;
    SpillFile lines;
    SpillFile texts;
    SpillFile vectors;
    std::vector<SourceInfo> sources;
    std::unordered_set<std::string> distinctTexts;
    std::vector<std::uint32_t> uses;
    std::uint32_t vectorCount = 0;
    std::uint64_t lineCount = 0;
    std::uint64_t textBytes = 0;
    std::uint64_t storedBytes = 0;
    std::uint32_t lastSource = 0;
    std::uint64_t lastLine = 0;
    bool hasLastLine = false;
    bool failed = false;
    bool finished = false;
};

CorpusWriter::CorpusWriter(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
void CorpusWriter::discardSpills(Impl *impl) noexcept
{
    if (impl == nullptr)
    {
        return;
    }
    impl->lines.output.close();
    impl->texts.output.close();
    impl->vectors.output.close();
    removeFile(impl->lines.path);
    removeFile(impl->texts.path);
    removeFile(impl->vectors.path);
    removeFile(impl->temporary);
}

CorpusWriter::~CorpusWriter() { discardSpills(impl_.get()); }
CorpusWriter::CorpusWriter(CorpusWriter &&) noexcept = default;
CorpusWriter &CorpusWriter::operator=(CorpusWriter &&other) noexcept
{
    if (this != &other)
    {
        discardSpills(impl_.get());
        impl_ = std::move(other.impl_);
    }
    return *this;
}

std::expected<CorpusWriter, EngineError> CorpusWriter::create(const std::filesystem::path &path, CorpusPolicy policy)
{
    const auto dimensions = validatePolicy(policy);
    if (!dimensions)
    {
        return std::unexpected(dimensions.error());
    }
    std::error_code error;
    if (std::filesystem::is_directory(path, error))
    {
        return std::unexpected(EngineError::FileOpenFailure);
    }
    auto impl = std::make_unique<Impl>();
    impl->destination = path;
    impl->policy = std::move(policy);
    impl->dimensions = *dimensions;
    auto lines = createSpill(path, "lines");
    if (!lines)
    {
        return std::unexpected(lines.error());
    }
    impl->lines = std::move(*lines);
    auto texts = createSpill(path, "texts");
    if (!texts)
    {
        return std::unexpected(texts.error());
    }
    impl->texts = std::move(*texts);
    auto vectors = createSpill(path, "vectors");
    if (!vectors)
    {
        return std::unexpected(vectors.error());
    }
    impl->vectors = std::move(*vectors);
    return CorpusWriter{std::move(impl)};
}

std::expected<std::uint32_t, EngineError> CorpusWriter::addSource(SourceInfo source)
{
    if (impl_->failed || impl_->finished || impl_->sources.size() >= MAX_SOURCES || source.path.empty() ||
        source.path.size() > MAX_TEXT_BYTES || source.jsonField.size() > MAX_TEXT_BYTES ||
        source.path.find('\0') != std::string::npos || source.jsonField.find('\0') != std::string::npos)
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    const auto id = static_cast<std::uint32_t>(impl_->sources.size());
    impl_->sources.push_back(std::move(source));
    return id;
}

std::expected<std::uint32_t, EngineError> CorpusWriter::reserveDistinct(std::string_view text)
{
    if (impl_->failed || impl_->finished || impl_->uses.size() >= MAX_DISTINCT || text.size() > MAX_TEXT_BYTES ||
        impl_->distinctTexts.contains(std::string{text}))
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    if (!writeU32(impl_->texts.output, static_cast<std::uint32_t>(text.size())) ||
        !writeRaw(impl_->texts.output, text.data(), text.size()))
    {
        impl_->failed = true;
        return std::unexpected(EngineError::FileOpenFailure);
    }
    const auto id = static_cast<std::uint32_t>(impl_->uses.size());
    impl_->distinctTexts.emplace(text);
    impl_->uses.push_back(0);
    impl_->textBytes += text.size();
    return id;
}

std::expected<void, EngineError> CorpusWriter::addVector(std::uint32_t id, std::span<const float> embedding)
{
    if (impl_->failed || impl_->finished || id != impl_->vectorCount || id >= impl_->uses.size())
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    if (const auto valid = validateVector(embedding, impl_->dimensions); !valid)
    {
        return std::unexpected(valid.error());
    }
    if (!writeRaw(impl_->vectors.output, embedding.data(), embedding.size_bytes()))
    {
        impl_->failed = true;
        return std::unexpected(EngineError::FileOpenFailure);
    }
    ++impl_->vectorCount;
    return {};
}

std::expected<std::uint32_t, EngineError> CorpusWriter::addDistinct(std::string_view text,
                                                                    std::span<const float> embedding)
{
    if (impl_->vectorCount != impl_->uses.size())
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    if (const auto valid = validateVector(embedding, impl_->dimensions); !valid)
    {
        return std::unexpected(valid.error());
    }
    const auto id = reserveDistinct(text);
    if (!id)
    {
        return std::unexpected(id.error());
    }
    if (const auto added = addVector(*id, embedding); !added)
    {
        return std::unexpected(added.error());
    }
    return *id;
}

std::expected<void, EngineError> CorpusWriter::addLines(std::span<const CorpusLine> lines)
{
    if (impl_->failed || impl_->finished || lines.size() > MAX_LINES - impl_->lineCount)
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    auto lastSource = impl_->lastSource;
    auto lastLine = impl_->lastLine;
    bool hasLastLine = impl_->hasLastLine;
    for (const CorpusLine &line : lines)
    {
        if (line.sourceIndex >= impl_->sources.size() || line.lineNumber == 0 ||
            (line.distinctId != NO_DISTINCT && line.distinctId >= impl_->uses.size()) ||
            (line.storedText && line.storedText->size() > MAX_TEXT_BYTES) ||
            (hasLastLine &&
             (line.sourceIndex < lastSource || (line.sourceIndex == lastSource && line.lineNumber <= lastLine))))
        {
            return std::unexpected(EngineError::CorruptDatabase);
        }
        const SourceInfo &source = impl_->sources[line.sourceIndex];
        if ((source.storedOnly && !line.storedText) ||
            (source.tracked && !rangeInside(line.byteOffset, line.byteLength, source.size)))
        {
            return std::unexpected(EngineError::CorruptDatabase);
        }
        lastSource = line.sourceIndex;
        lastLine = line.lineNumber;
        hasLastLine = true;
    }
    for (const CorpusLine &line : lines)
    {
        const std::uint32_t flags = (line.arrayElement ? LINE_ARRAY_ELEMENT : 0U) |
                                    (line.storedText ? LINE_STORED_TEXT : 0U) |
                                    (line.distinctId == NO_DISTINCT ? LINE_SKIPPED : 0U);
        const std::string_view text = line.storedText.value_or(std::string_view{});
        if (!writeU64(impl_->lines.output, line.lineNumber) || !writeU64(impl_->lines.output, line.byteOffset) ||
            !writeU64(impl_->lines.output, line.byteLength) || !writeU32(impl_->lines.output, line.sourceIndex) ||
            !writeU32(impl_->lines.output, line.distinctId) || !writeU32(impl_->lines.output, flags) ||
            !writeU32(impl_->lines.output, static_cast<std::uint32_t>(text.size())) ||
            !writeRaw(impl_->lines.output, text.data(), text.size()))
        {
            impl_->failed = true;
            return std::unexpected(EngineError::FileOpenFailure);
        }
        if (line.distinctId != NO_DISTINCT)
        {
            ++impl_->uses[line.distinctId];
        }
        impl_->storedBytes += text.size();
        ++impl_->lineCount;
    }
    impl_->lastSource = lastSource;
    impl_->lastLine = lastLine;
    impl_->hasLastLine = hasLastLine;
    return {};
}

std::expected<void, EngineError> CorpusWriter::finish()
{
    if (impl_->failed || impl_->finished || impl_->vectorCount != impl_->uses.size() ||
        std::ranges::any_of(impl_->uses, [](std::uint32_t uses) { return uses == 0; }))
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    impl_->lines.output.close();
    impl_->texts.output.close();
    impl_->vectors.output.close();
    if (!impl_->lines.output || !impl_->texts.output || !impl_->vectors.output)
    {
        impl_->failed = true;
        return std::unexpected(EngineError::FileOpenFailure);
    }
    std::ifstream lines(impl_->lines.path, std::ios::binary);
    std::ifstream texts(impl_->texts.path, std::ios::binary);
    std::ifstream vectors(impl_->vectors.path, std::ios::binary);
    if (!lines || !texts || !vectors)
    {
        impl_->failed = true;
        return std::unexpected(EngineError::FileOpenFailure);
    }
    auto temporary = createSpill(impl_->destination, "tmp");
    if (!temporary)
    {
        impl_->failed = true;
        return std::unexpected(temporary.error());
    }
    impl_->temporary = temporary->path;
    std::ofstream &output = temporary->output;
    const auto fail = [&]() -> std::expected<void, EngineError>
    {
        impl_->failed = true;
        return std::unexpected(EngineError::FileOpenFailure);
    };

    const std::string policy = serializePolicy(impl_->policy);
    const std::uint64_t sourceOffset = alignUp(HEADER_BYTES + policy.size(), 8);
    std::uint64_t lineOffset = sourceOffset + (impl_->sources.size() * SOURCE_RECORD_BYTES);
    for (const SourceInfo &source : impl_->sources)
    {
        lineOffset += source.path.size() + source.jsonField.size();
    }
    const std::uint64_t distinctOffset = lineOffset + (impl_->lineCount * LINE_RECORD_BYTES);
    const std::uint64_t textOffset = distinctOffset + (impl_->uses.size() * TEXT_RECORD_BYTES);
    const std::uint64_t storedOffset = textOffset + impl_->textBytes;
    const std::uint64_t matrixOffset = alignUp(storedOffset + impl_->storedBytes, 64);
    const std::uint64_t matrixBytes =
        impl_->uses.size() * static_cast<std::uint64_t>(impl_->dimensions) * sizeof(float);

    // The two zero fields reserve a lexical-section slot without introducing a section directory.
    if (!writeRaw(output, MAGIC.data(), MAGIC.size()) || !writeU32(output, CORPUS_VERSION) ||
        !writeU32(output, static_cast<std::uint32_t>(policy.size())) || !writeU32(output, impl_->dimensions) ||
        !writeU64(output, impl_->sources.size()) || !writeU64(output, impl_->lineCount) ||
        !writeU64(output, sourceOffset) || !writeU64(output, lineOffset) || !writeU64(output, matrixOffset) ||
        !writeU64(output, impl_->uses.size()) || !writeU64(output, distinctOffset) || !writeU64(output, textOffset) ||
        !writeU64(output, storedOffset) || !writeU64(output, 0) || !writeU64(output, 0) ||
        !writeU64(output, HEADER_BYTES) || !writeU64(output, 0) || !writeU64(output, 0) ||
        !writeRaw(output, policy.data(), policy.size()) ||
        !writePadding(output, sourceOffset - HEADER_BYTES - policy.size()))
    {
        return fail();
    }

    std::uint64_t sourceCursor = sourceOffset + (impl_->sources.size() * SOURCE_RECORD_BYTES);
    for (const SourceInfo &source : impl_->sources)
    {
        const std::uint32_t flags =
            (source.tracked ? SOURCE_TRACKED : 0U) | (source.storedOnly ? SOURCE_STORED_ONLY : 0U);
        if (!writeU64(output, sourceCursor) || !writeU32(output, static_cast<std::uint32_t>(source.path.size())) ||
            !writeU32(output, flags) || !writeU64(output, source.size) ||
            !writeU64(output, std::bit_cast<std::uint64_t>(source.modifiedTime)) ||
            !writeU64(output, sourceCursor + source.path.size()) ||
            !writeU32(output, static_cast<std::uint32_t>(source.jsonField.size())) || !writeU32(output, 0))
        {
            return fail();
        }
        sourceCursor += source.path.size() + source.jsonField.size();
    }
    for (const SourceInfo &source : impl_->sources)
    {
        if (!writeRaw(output, source.path.data(), source.path.size()) ||
            !writeRaw(output, source.jsonField.data(), source.jsonField.size()))
        {
            return fail();
        }
    }

    std::uint64_t storedCursor = storedOffset;
    SpilledLine line;
    for (std::uint64_t index = 0; index < impl_->lineCount; ++index)
    {
        if (!readSpilledLine(lines, line))
        {
            return fail();
        }
        const bool hasText = (line.flags & LINE_STORED_TEXT) != 0;
        if (!writeU64(output, line.lineNumber) || !writeU64(output, line.byteOffset) ||
            !writeU64(output, line.byteLength) || !writeU32(output, line.sourceIndex) ||
            !writeU32(output, line.distinctId) || !writeU64(output, hasText ? storedCursor : 0) ||
            !writeU32(output, static_cast<std::uint32_t>(line.storedText.size())) || !writeU32(output, line.flags) ||
            !writeU64(output, 0))
        {
            return fail();
        }
        storedCursor += line.storedText.size();
    }
    if (!atEnd(lines))
    {
        return fail();
    }

    std::uint64_t textCursor = textOffset;
    std::string text;
    for (std::size_t index = 0; index < impl_->uses.size(); ++index)
    {
        if (!readSpilledText(texts, text) || !writeU64(output, textCursor) ||
            !writeU32(output, static_cast<std::uint32_t>(text.size())) || !writeU32(output, 0))
        {
            return fail();
        }
        textCursor += text.size();
    }
    if (!atEnd(texts) || textCursor != storedOffset || !rewind(texts))
    {
        return fail();
    }
    for (std::size_t index = 0; index < impl_->uses.size(); ++index)
    {
        if (!readSpilledText(texts, text) || !writeRaw(output, text.data(), text.size()))
        {
            return fail();
        }
    }
    if (!atEnd(texts) || !rewind(lines))
    {
        return fail();
    }
    for (std::uint64_t index = 0; index < impl_->lineCount; ++index)
    {
        if (!readSpilledLine(lines, line) || !writeRaw(output, line.storedText.data(), line.storedText.size()))
        {
            return fail();
        }
    }
    if (!atEnd(lines) || storedCursor != storedOffset + impl_->storedBytes ||
        !writePadding(output, matrixOffset - storedCursor) || !copyBytes(vectors, output, matrixBytes))
    {
        return fail();
    }
    output.close();
    if (!output || !sealCorpus(impl_->temporary))
    {
        return fail();
    }
    if (const auto replaced = nesso::platform::replaceFileAtomically(impl_->temporary, impl_->destination); !replaced)
    {
        return fail();
    }
    impl_->temporary.clear();
    impl_->finished = true;
    return {};
}

struct MappedCorpus::Impl
{
    nesso::platform::MappedFile file;
    CorpusPolicy policy;
    std::uint32_t dimensions = 0;
    std::vector<SourceInfo> sources;
    std::vector<CorpusLine> lines;
    std::vector<std::string_view> texts;
    std::span<const float> matrix;
};

MappedCorpus::MappedCorpus(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
MappedCorpus::~MappedCorpus() = default;
MappedCorpus::MappedCorpus(MappedCorpus &&) noexcept = default;
MappedCorpus &MappedCorpus::operator=(MappedCorpus &&) noexcept = default;

const CorpusPolicy &MappedCorpus::policy() const noexcept { return impl_->policy; }
std::uint32_t MappedCorpus::dimensions() const noexcept { return impl_->dimensions; }
std::span<const SourceInfo> MappedCorpus::sources() const noexcept { return impl_->sources; }
std::span<const CorpusLine> MappedCorpus::lines() const noexcept { return impl_->lines; }
std::span<const std::string_view> MappedCorpus::texts() const noexcept { return impl_->texts; }
std::span<const float> MappedCorpus::matrix() const noexcept { return impl_->matrix; }

std::expected<MappedCorpus, EngineError> mapCorpusFile(const std::filesystem::path &path,
                                                       std::uint32_t *observedVersion)
{
    auto mapped = nesso::platform::MappedFile::open(path);
    if (!mapped)
    {
        return std::unexpected(mapped.error() == nesso::platform::FileError::OpenFailure
                                   ? EngineError::FileOpenFailure
                                   : EngineError::CorruptDatabase);
    }
    const std::span<const std::byte> bytes = mapped->bytes();
    std::uint32_t version = 0;
    if (bytes.size() < 8 || std::memcmp(bytes.data(), MAGIC.data(), MAGIC.size()) != 0 || !readU32(bytes, 4, version))
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    if (observedVersion != nullptr)
    {
        *observedVersion = version;
    }
    if (version != CORPUS_VERSION)
    {
        return std::unexpected(EngineError::UnsupportedVersion);
    }
    if (bytes.size() < HEADER_BYTES)
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    const CorpusChecksum checksum = corpusChecksum(bytes);
    if (!std::ranges::equal(checksum, bytes.subspan(CORPUS_CHECKSUM_OFFSET, CORPUS_CHECKSUM_BYTES)))
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    Layout layout;
    if (bytes.size() < HEADER_BYTES || !readU32(bytes, 8, layout.policyBytes) ||
        !readU32(bytes, 12, layout.dimensions) || !readU64(bytes, 16, layout.sourceCount) ||
        !readU64(bytes, 24, layout.lineCount) || !readU64(bytes, 32, layout.sourceOffset) ||
        !readU64(bytes, 40, layout.lineOffset) || !readU64(bytes, 48, layout.matrixOffset) ||
        !readU64(bytes, 56, layout.distinctCount) || !readU64(bytes, 64, layout.distinctOffset) ||
        !readU64(bytes, 72, layout.textOffset) || !readU64(bytes, 80, layout.storedOffset) ||
        !readU64(bytes, 88, layout.lexicalOffset) || !readU64(bytes, 96, layout.lexicalBytes) ||
        !readU64(bytes, 104, layout.policyOffset))
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    if (layout.policyBytes > MAX_POLICY_BYTES || layout.dimensions > MAX_DIMENSIONS ||
        layout.sourceCount > MAX_SOURCES || layout.lineCount > MAX_LINES || layout.distinctCount > MAX_DISTINCT ||
        (layout.distinctCount > 0 && layout.dimensions == 0) || layout.lexicalOffset != 0 || layout.lexicalBytes != 0 ||
        layout.policyOffset != HEADER_BYTES || layout.sourceOffset != alignUp(HEADER_BYTES + layout.policyBytes, 8) ||
        layout.sourceOffset > layout.lineOffset || layout.lineOffset > layout.distinctOffset ||
        layout.distinctOffset > layout.textOffset || layout.textOffset > layout.storedOffset ||
        layout.storedOffset > layout.matrixOffset || layout.matrixOffset > bytes.size() ||
        layout.matrixOffset % 64 != 0 ||
        layout.sourceCount > (layout.lineOffset - layout.sourceOffset) / SOURCE_RECORD_BYTES ||
        layout.distinctOffset - layout.lineOffset != layout.lineCount * LINE_RECORD_BYTES ||
        layout.textOffset - layout.distinctOffset != layout.distinctCount * TEXT_RECORD_BYTES ||
        !rangeInside(layout.policyOffset, layout.policyBytes, layout.sourceOffset) ||
        !isZero(bytes, HEADER_BYTES + layout.policyBytes, layout.sourceOffset))
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    const std::uint64_t matrixBytes = layout.distinctCount * layout.dimensions * sizeof(float);
    if (!rangeInside(layout.matrixOffset, matrixBytes, bytes.size()) ||
        layout.matrixOffset + matrixBytes != bytes.size())
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    auto policy = parsePolicy(viewBytes(bytes, layout.policyOffset, layout.policyBytes));
    if (!policy)
    {
        return std::unexpected(policy.error());
    }
    const auto dimensions = validatePolicy(*policy);
    if (!dimensions || *dimensions != layout.dimensions)
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }

    auto impl = std::make_unique<MappedCorpus::Impl>();
    impl->policy = std::move(*policy);
    impl->dimensions = layout.dimensions;
    impl->sources.reserve(static_cast<std::size_t>(layout.sourceCount));
    std::uint64_t sourceCursor = layout.sourceOffset + (layout.sourceCount * SOURCE_RECORD_BYTES);
    for (std::uint64_t index = 0; index < layout.sourceCount; ++index)
    {
        const std::uint64_t record = layout.sourceOffset + (index * SOURCE_RECORD_BYTES);
        std::uint64_t pathOffset = 0;
        std::uint32_t pathLength = 0;
        std::uint32_t flags = 0;
        std::uint64_t size = 0;
        std::uint64_t modified = 0;
        std::uint64_t fieldOffset = 0;
        std::uint32_t fieldLength = 0;
        std::uint32_t reserved = 0;
        if (!readU64(bytes, record, pathOffset) || !readU32(bytes, record + 8, pathLength) ||
            !readU32(bytes, record + 12, flags) || !readU64(bytes, record + 16, size) ||
            !readU64(bytes, record + 24, modified) || !readU64(bytes, record + 32, fieldOffset) ||
            !readU32(bytes, record + 40, fieldLength) || !readU32(bytes, record + 44, reserved) || pathLength == 0 ||
            pathLength > MAX_TEXT_BYTES || fieldLength > MAX_TEXT_BYTES ||
            (flags & ~(SOURCE_TRACKED | SOURCE_STORED_ONLY)) != 0 || reserved != 0 || pathOffset != sourceCursor ||
            !rangeInside(pathOffset, pathLength, layout.lineOffset) || fieldOffset != pathOffset + pathLength ||
            !rangeInside(fieldOffset, fieldLength, layout.lineOffset))
        {
            return std::unexpected(EngineError::CorruptDatabase);
        }
        SourceInfo source{.path = std::string{viewBytes(bytes, pathOffset, pathLength)},
                          .size = size,
                          .modifiedTime = std::bit_cast<std::int64_t>(modified),
                          .tracked = (flags & SOURCE_TRACKED) != 0,
                          .jsonField = std::string{viewBytes(bytes, fieldOffset, fieldLength)},
                          .storedOnly = (flags & SOURCE_STORED_ONLY) != 0};
        if (source.path.find('\0') != std::string::npos || source.jsonField.find('\0') != std::string::npos)
        {
            return std::unexpected(EngineError::CorruptDatabase);
        }
        impl->sources.push_back(std::move(source));
        sourceCursor = fieldOffset + fieldLength;
    }
    if (sourceCursor != layout.lineOffset)
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }

    impl->texts.reserve(static_cast<std::size_t>(layout.distinctCount));
    std::unordered_set<std::string_view> seenTexts;
    std::uint64_t textCursor = layout.textOffset;
    for (std::uint64_t index = 0; index < layout.distinctCount; ++index)
    {
        const std::uint64_t record = layout.distinctOffset + (index * TEXT_RECORD_BYTES);
        std::uint64_t offset = 0;
        std::uint32_t length = 0;
        std::uint32_t reserved = 0;
        if (!readU64(bytes, record, offset) || !readU32(bytes, record + 8, length) ||
            !readU32(bytes, record + 12, reserved) || length > MAX_TEXT_BYTES || reserved != 0 ||
            offset != textCursor || !rangeInside(offset, length, layout.storedOffset))
        {
            return std::unexpected(EngineError::CorruptDatabase);
        }
        const std::string_view text = viewBytes(bytes, offset, length);
        if (!seenTexts.insert(text).second)
        {
            return std::unexpected(EngineError::CorruptDatabase);
        }
        impl->texts.push_back(text);
        textCursor += length;
    }
    if (textCursor != layout.storedOffset)
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }

    impl->lines.reserve(static_cast<std::size_t>(layout.lineCount));
    std::vector<bool> used(static_cast<std::size_t>(layout.distinctCount));
    std::uint64_t storedCursor = layout.storedOffset;
    for (std::uint64_t index = 0; index < layout.lineCount; ++index)
    {
        const std::uint64_t record = layout.lineOffset + (index * LINE_RECORD_BYTES);
        CorpusLine line;
        std::uint64_t textOffset = 0;
        std::uint32_t textLength = 0;
        std::uint32_t flags = 0;
        std::uint64_t reserved = 0;
        if (!readU64(bytes, record, line.lineNumber) || !readU64(bytes, record + 8, line.byteOffset) ||
            !readU64(bytes, record + 16, line.byteLength) || !readU32(bytes, record + 24, line.sourceIndex) ||
            !readU32(bytes, record + 28, line.distinctId) || !readU64(bytes, record + 32, textOffset) ||
            !readU32(bytes, record + 40, textLength) || !readU32(bytes, record + 44, flags) ||
            !readU64(bytes, record + 48, reserved) || line.lineNumber == 0 ||
            line.sourceIndex >= impl->sources.size() ||
            (flags & ~(LINE_ARRAY_ELEMENT | LINE_STORED_TEXT | LINE_SKIPPED)) != 0 || reserved != 0 ||
            ((flags & LINE_SKIPPED) != 0) != (line.distinctId == NO_DISTINCT) ||
            (line.distinctId != NO_DISTINCT && line.distinctId >= layout.distinctCount) || textLength > MAX_TEXT_BYTES)
        {
            return std::unexpected(EngineError::CorruptDatabase);
        }
        if (!impl->lines.empty())
        {
            const CorpusLine &previous = impl->lines.back();
            if (line.sourceIndex < previous.sourceIndex ||
                (line.sourceIndex == previous.sourceIndex && line.lineNumber <= previous.lineNumber))
            {
                return std::unexpected(EngineError::CorruptDatabase);
            }
        }
        const SourceInfo &source = impl->sources[line.sourceIndex];
        if ((source.tracked && !rangeInside(line.byteOffset, line.byteLength, source.size)) ||
            (source.storedOnly && (flags & LINE_STORED_TEXT) == 0))
        {
            return std::unexpected(EngineError::CorruptDatabase);
        }
        if ((flags & LINE_STORED_TEXT) != 0)
        {
            if (textOffset != storedCursor || !rangeInside(textOffset, textLength, layout.matrixOffset))
            {
                return std::unexpected(EngineError::CorruptDatabase);
            }
            line.storedText = viewBytes(bytes, textOffset, textLength);
            storedCursor += textLength;
        }
        else if (textOffset != 0 || textLength != 0)
        {
            return std::unexpected(EngineError::CorruptDatabase);
        }
        if (line.distinctId != NO_DISTINCT)
        {
            used[line.distinctId] = true;
        }
        line.arrayElement = (flags & LINE_ARRAY_ELEMENT) != 0;
        impl->lines.push_back(line);
    }
    if (std::ranges::any_of(used, [](bool value) { return !value; }) ||
        !isZero(bytes, storedCursor, layout.matrixOffset))
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }

    impl->file = std::move(*mapped);
    const auto stable = impl->file.bytes();
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    impl->matrix = {reinterpret_cast<const float *>(stable.data() + layout.matrixOffset),
                    layout.distinctCount * layout.dimensions};
    return MappedCorpus{std::move(impl)};
}

std::expected<std::vector<CorpusChunk>, EngineError> readCorpusFile(const std::filesystem::path &path)
{
    auto mapped = mapCorpusFile(path);
    if (!mapped)
    {
        return std::unexpected(mapped.error());
    }
    std::vector<CorpusChunk> chunks;
    for (const CorpusLine &line : mapped->lines())
    {
        if (line.distinctId == NO_DISTINCT)
        {
            continue;
        }
        const std::size_t dimensions = mapped->dimensions();
        const auto embedding =
            mapped->matrix().subspan(static_cast<std::size_t>(line.distinctId) * dimensions, dimensions);
        chunks.push_back(
            CorpusChunk{.chunk = {.text = std::string{line.storedText.value_or(mapped->texts()[line.distinctId])},
                                  .lineNumber = line.lineNumber,
                                  .source = mapped->sources()[line.sourceIndex].path,
                                  .byteOffset = line.byteOffset,
                                  .arrayElement = line.arrayElement,
                                  .byteLength = line.byteLength},
                        .embedding = {embedding.begin(), embedding.end()}});
    }
    return chunks;
}

} // namespace nesso::core

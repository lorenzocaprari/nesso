// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_all.hpp>
#include <core/sha256.hpp>

#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <unistd.h>

namespace
{

std::string digest(std::string_view text)
{
    return nesso::core::hexSha256(nesso::core::sha256(std::as_bytes(std::span{text})));
}

} // namespace

TEST_CASE("SHA-256 matches standard vectors", "[sha256][core]")
{
    CHECK(digest("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(digest("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(digest("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    CHECK(digest(std::string(55, 'a')) == "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318");
    CHECK(digest(std::string(56, 'a')) == "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a");
    CHECK(digest(std::string(63, 'a')) == "7d3e74a05d7db15bce4ad9ec0658ea98e3f06eeecf16b4c6fff2da457ddc2f34");
    CHECK(digest(std::string(64, 'a')) == "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb");
    CHECK(digest(std::string(65, 'a')) == "635361c48bb9eab14198e76ea8ab7f1a41685d6ad62aa9146d301d4f17eb0ae0");
    CHECK(digest(std::string(1'000'000, 'a')) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST_CASE("SHA-256 streams file bytes and reports open failures", "[sha256][core]")
{
    const auto path = std::filesystem::temp_directory_path() / ("nesso_sha256_" + std::to_string(getpid()) + ".bin");
    std::string bytes(150'001, 'x');
    bytes[100] = '\0';
    {
        std::ofstream output(path, std::ios::binary);
        REQUIRE(output);
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        REQUIRE(output);
    }
    const auto hashed = nesso::core::sha256File(path);
    REQUIRE(hashed);
    CHECK(nesso::core::hexSha256(*hashed) == digest(bytes));
    std::filesystem::remove(path);
    const auto missing = nesso::core::sha256File(path);
    REQUIRE_FALSE(missing);
    CHECK(missing.error() == nesso::core::HashError::OpenFailure);
    const auto directory = nesso::core::sha256File(std::filesystem::temp_directory_path());
    REQUIRE_FALSE(directory);
    CHECK(directory.error() == nesso::core::HashError::ReadFailure);
}

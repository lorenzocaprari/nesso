// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_all.hpp>
#include <nesso_cli/check_stdout.hpp>

#include <cstdio>
#include <filesystem>
#include <memory>

namespace
{

struct CloseFile
{
    void operator()(std::FILE *file) const noexcept
    {
        if (file != nullptr)
        {
            static_cast<void>(std::fclose(file));
        }
    }
};

} // namespace

TEST_CASE("StdoutWriteFailed is false for a writable temp file", "[cli][stdout]")
{
    const auto path = std::filesystem::temp_directory_path() / "nesso-check-stdout-ok";
    const std::unique_ptr<std::FILE, CloseFile> stream{std::fopen(path.string().c_str(), "we")};
    REQUIRE(stream != nullptr);
    REQUIRE(std::fputs("ok\n", stream.get()) >= 0);
    CHECK_FALSE(nesso::cli::StdoutWriteFailed(stream.get()));
    std::filesystem::remove(path);
}

TEST_CASE("StdoutWriteFailed is true for /dev/full", "[cli][stdout]")
{
    const std::unique_ptr<std::FILE, CloseFile> stream{std::fopen("/dev/full", "we")};
    REQUIRE(stream != nullptr);
    (void)std::fputc('x', stream.get());
    CHECK(nesso::cli::StdoutWriteFailed(stream.get()));
}

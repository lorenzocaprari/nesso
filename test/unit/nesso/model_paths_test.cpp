// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_all.hpp>
#include <nesso/model_paths.hpp>

#include "../env_set.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace
{

void WriteEmpty(const std::filesystem::path &path)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path);
    REQUIRE(out.good());
}

void SeedModelDir(const std::filesystem::path &dir)
{
    WriteEmpty(dir / nesso::VOCAB_FILENAME);
    WriteEmpty(dir / nesso::MODEL_FILENAME);
}

class EnvGuard
{
  public:
    EnvGuard()
    {
        Snapshot("NESSO_MODEL_DIR");
#ifdef _WIN32
        Snapshot("LOCALAPPDATA");
#else
        Snapshot("XDG_DATA_HOME");
        Snapshot("HOME");
#endif
    }

    EnvGuard(const EnvGuard &) = delete;
    auto operator=(const EnvGuard &) -> EnvGuard & = delete;
    EnvGuard(EnvGuard &&) = delete;
    auto operator=(EnvGuard &&) -> EnvGuard & = delete;

    ~EnvGuard()
    {
        for (const Slot &entry : slots_)
        {
            if (entry.present)
            {
                static_cast<void>(nesso::test::EnvSet(entry.key.c_str(), entry.value));
            }
            else
            {
                static_cast<void>(nesso::test::EnvClear(entry.key.c_str()));
            }
        }
    }

    static void Set(const char *key, const std::string &value) { REQUIRE(nesso::test::EnvSet(key, value)); }

    static void clear(const char *key) { REQUIRE(nesso::test::EnvClear(key)); }

  private:
    struct Slot
    {
        std::string key;
        std::string value;
        bool present = false;
    };

    std::vector<Slot> slots_;

    void Snapshot(const char *key)
    {
        Slot entry;
        entry.key = key;
        // NOLINTNEXTLINE(concurrency-mt-unsafe)
        if (const char *value = std::getenv(key); value != nullptr)
        {
            entry.present = true;
            entry.value = value;
        }
        slots_.push_back(std::move(entry));
    }
};

} // namespace

TEST_CASE("ContainsModelFiles requires both MiniLM files")
{
    const auto tmp = std::filesystem::temp_directory_path() / "nesso-model-paths-contains";
    std::filesystem::remove_all(tmp);
    std::filesystem::create_directories(tmp);

    REQUIRE_FALSE(nesso::ContainsModelFiles(tmp));
    WriteEmpty(tmp / nesso::VOCAB_FILENAME);
    REQUIRE_FALSE(nesso::ContainsModelFiles(tmp));
    WriteEmpty(tmp / nesso::MODEL_FILENAME);
    REQUIRE(nesso::ContainsModelFiles(tmp));
    std::filesystem::remove_all(tmp);
}

TEST_CASE("ResolveDefaultModelDir search order")
{
    EnvGuard env;
    EnvGuard::clear("NESSO_MODEL_DIR");
#ifdef _WIN32
    EnvGuard::clear("LOCALAPPDATA");
#else
    EnvGuard::clear("XDG_DATA_HOME");
    EnvGuard::clear("HOME");
#endif

    const auto root = std::filesystem::temp_directory_path() / "nesso-model-paths-order";
    std::filesystem::remove_all(root);
    const auto cwd = root / "cwd";
    const auto packaged = root / "packaged";
    std::filesystem::create_directories(cwd);
    std::filesystem::create_directories(packaged);

    SECTION("falls back to packaged dir")
    {
        REQUIRE(nesso::ResolveDefaultModelDir({.cwd = cwd, .packaged_dir = packaged}) == packaged);
    }

    SECTION("NESSO_MODEL_DIR wins even if incomplete")
    {
        const auto env_dir = root / "env";
        std::filesystem::create_directories(env_dir);
        EnvGuard::Set("NESSO_MODEL_DIR", env_dir.string());
        SeedModelDir(cwd / "models");
        REQUIRE(nesso::ResolveDefaultModelDir({.cwd = cwd, .packaged_dir = packaged}) == env_dir);
    }

    SECTION("cwd/models when complete")
    {
        SeedModelDir(cwd / "models");
        REQUIRE(nesso::ResolveDefaultModelDir({.cwd = cwd, .packaged_dir = packaged}) == cwd / "models");
    }

#ifdef _WIN32
    SECTION("LOCALAPPDATA/nesso when complete")
    {
        const auto local = root / "local";
        SeedModelDir(local / "nesso");
        EnvGuard::Set("LOCALAPPDATA", local.string());
        REQUIRE(nesso::ResolveDefaultModelDir({.cwd = cwd, .packaged_dir = packaged}) == local / "nesso");
    }
#else
    SECTION("XDG_DATA_HOME/nesso when complete")
    {
        const auto xdg = root / "xdg";
        SeedModelDir(xdg / "nesso");
        EnvGuard::Set("XDG_DATA_HOME", xdg.string());
        REQUIRE(nesso::ResolveDefaultModelDir({.cwd = cwd, .packaged_dir = packaged}) == xdg / "nesso");
    }

    SECTION("HOME/.local/share/nesso when XDG unset")
    {
        const auto home = root / "home";
        SeedModelDir(home / ".local" / "share" / "nesso");
        EnvGuard::Set("HOME", home.string());
        REQUIRE(nesso::ResolveDefaultModelDir({.cwd = cwd, .packaged_dir = packaged}) ==
                home / ".local" / "share" / "nesso");
    }
#endif

    std::filesystem::remove_all(root);
}

// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_all.hpp>
#include <nesso/model_paths.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

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
        Snapshot("XDG_DATA_HOME");
        Snapshot("HOME");
    }

    EnvGuard(const EnvGuard &) = delete;
    EnvGuard &operator=(const EnvGuard &) = delete;
    EnvGuard(EnvGuard &&) = delete;
    EnvGuard &operator=(EnvGuard &&) = delete;

    ~EnvGuard()
    {
        Restore("NESSO_MODEL_DIR");
        Restore("XDG_DATA_HOME");
        Restore("HOME");
    }

    static void Set(const char *key, const std::string &value) { REQUIRE(setenv(key, value.c_str(), 1) == 0); }

    static void clear(const char *key) { REQUIRE(unsetenv(key) == 0); }

  private:
    struct Slot
    {
        std::string key;
        std::string value;
        bool present = false;
    };

    Slot nesso_model_dir_;
    Slot xdg_data_home_;
    Slot home_;

    Slot &GetSlot(const char *key)
    {
        if (std::string_view(key) == "NESSO_MODEL_DIR")
        {
            return nesso_model_dir_;
        }
        if (std::string_view(key) == "XDG_DATA_HOME")
        {
            return xdg_data_home_;
        }
        return home_;
    }

    void Snapshot(const char *key)
    {
        Slot &entry = GetSlot(key);
        entry.key = key;
        if (const char *value = std::getenv(key); value != nullptr)
        {
            entry.present = true;
            entry.value = value;
        }
    }

    void Restore(const char *key)
    {
        const Slot &entry = GetSlot(key);
        if (entry.present)
        {
            setenv(key, entry.value.c_str(), 1);
        }
        else
        {
            unsetenv(key);
        }
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
    EnvGuard::clear("XDG_DATA_HOME");
    EnvGuard::clear("HOME");

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

    std::filesystem::remove_all(root);
}

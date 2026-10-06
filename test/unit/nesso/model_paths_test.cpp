// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#include <catch2/catch_all.hpp>
#include <model_paths.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace
{

void writeEmpty(const std::filesystem::path &path)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path);
    REQUIRE(out.good());
}

void seedModelDir(const std::filesystem::path &dir)
{
    writeEmpty(dir / nesso::VOCAB_FILENAME);
    writeEmpty(dir / nesso::MODEL_FILENAME);
}

class EnvGuard
{
  public:
    EnvGuard()
    {
        snapshot("NESSO_MODEL_DIR");
        snapshot("XDG_DATA_HOME");
        snapshot("HOME");
    }

    EnvGuard(const EnvGuard &) = delete;
    EnvGuard &operator=(const EnvGuard &) = delete;
    EnvGuard(EnvGuard &&) = delete;
    EnvGuard &operator=(EnvGuard &&) = delete;

    ~EnvGuard()
    {
        restore("NESSO_MODEL_DIR");
        restore("XDG_DATA_HOME");
        restore("HOME");
    }

    static void set(const char *key, const std::string &value) { REQUIRE(setenv(key, value.c_str(), 1) == 0); }

    static void clear(const char *key) { REQUIRE(unsetenv(key) == 0); }

  private:
    struct Slot
    {
        std::string key;
        std::string value;
        bool present = false;
    };

    Slot nessoModelDir;
    Slot xdgDataHome;
    Slot home;

    Slot &slot(const char *key)
    {
        if (std::string_view(key) == "NESSO_MODEL_DIR")
        {
            return nessoModelDir;
        }
        if (std::string_view(key) == "XDG_DATA_HOME")
        {
            return xdgDataHome;
        }
        return home;
    }

    void snapshot(const char *key)
    {
        Slot &entry = slot(key);
        entry.key = key;
        if (const char *value = std::getenv(key); value != nullptr)
        {
            entry.present = true;
            entry.value = value;
        }
    }

    void restore(const char *key)
    {
        const Slot &entry = slot(key);
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

TEST_CASE("containsModelFiles requires both MiniLM files")
{
    const auto tmp = std::filesystem::temp_directory_path() / "nesso-model-paths-contains";
    std::filesystem::remove_all(tmp);
    std::filesystem::create_directories(tmp);

    REQUIRE_FALSE(nesso::containsModelFiles(tmp));
    writeEmpty(tmp / nesso::VOCAB_FILENAME);
    REQUIRE_FALSE(nesso::containsModelFiles(tmp));
    writeEmpty(tmp / nesso::MODEL_FILENAME);
    REQUIRE(nesso::containsModelFiles(tmp));
    std::filesystem::remove_all(tmp);
}

TEST_CASE("resolveDefaultModelDir search order")
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

    SECTION("falls back to packaged dir") { REQUIRE(nesso::resolveDefaultModelDir(cwd, packaged) == packaged); }

    SECTION("NESSO_MODEL_DIR wins even if incomplete")
    {
        const auto envDir = root / "env";
        std::filesystem::create_directories(envDir);
        EnvGuard::set("NESSO_MODEL_DIR", envDir.string());
        seedModelDir(cwd / "models");
        REQUIRE(nesso::resolveDefaultModelDir(cwd, packaged) == envDir);
    }

    SECTION("cwd/models when complete")
    {
        seedModelDir(cwd / "models");
        REQUIRE(nesso::resolveDefaultModelDir(cwd, packaged) == cwd / "models");
    }

    SECTION("XDG_DATA_HOME/nesso when complete")
    {
        const auto xdg = root / "xdg";
        seedModelDir(xdg / "nesso");
        EnvGuard::set("XDG_DATA_HOME", xdg.string());
        REQUIRE(nesso::resolveDefaultModelDir(cwd, packaged) == xdg / "nesso");
    }

    SECTION("HOME/.local/share/nesso when XDG unset")
    {
        const auto home = root / "home";
        seedModelDir(home / ".local" / "share" / "nesso");
        EnvGuard::set("HOME", home.string());
        REQUIRE(nesso::resolveDefaultModelDir(cwd, packaged) == home / ".local" / "share" / "nesso");
    }

    std::filesystem::remove_all(root);
}

#include <catch2/catch_all.hpp>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mach_core/core_types.hpp>
#include <mach_core/log_line_store.hpp>

namespace fs = std::filesystem;
using namespace mach_core;

namespace
{
fs::path makeTempPath(const char *prefix)
{
    return fs::temp_directory_path() / (std::string(prefix) + std::to_string(std::rand()));
}

class FileGuard
{
  public:
    explicit FileGuard(fs::path path) : m_path(std::move(path)) {}
    ~FileGuard() { fs::remove(m_path); }

  private:
    fs::path m_path;
};
} // namespace

SCENARIO("LogLineStore persists JSONL sidecars", "[LogLineStore][core][Unit]")
{
    const auto path = makeTempPath("vecgrep_sidecar_");
    FileGuard guard(path);

    GIVEN("A newly opened sidecar")
    {
        LogLineStore store;
        REQUIRE(store.open(path).has_value());
        REQUIRE(store.size() == 0);

        WHEN("appending two log lines")
        {
            REQUIRE(store.append(0, 1, "boot ok").has_value());
            REQUIRE(store.append(1, 2, "CAN timeout 0x1A4").has_value());

            THEN("they can be fetched by id")
            {
                auto first = store.get(0);
                auto second = store.get(1);
                REQUIRE(first.has_value());
                REQUIRE(second.has_value());
                REQUIRE(first->lineNo == 1);
                REQUIRE(first->text == "boot ok");
                REQUIRE(second->text == "CAN timeout 0x1A4");
            }

            AND_WHEN("reopening the sidecar")
            {
                LogLineStore reopened;
                REQUIRE(reopened.open(path).has_value());
                REQUIRE(reopened.size() == 2);
                auto line = reopened.get(1);
                REQUIRE(line.has_value());
                REQUIRE(line->text == "CAN timeout 0x1A4");
            }
        }
    }
}

TEST_CASE("sidecarPathFor appends the VecGrep suffix", "[LogLineStore][core][Unit]")
{
    REQUIRE(sidecarPathFor("index.vecgrep") == fs::path("index.vecgrep.lines.jsonl"));
}

TEST_CASE("LogLineStore rejects out-of-order ids", "[LogLineStore][core][Unit]")
{
    const auto path = makeTempPath("vecgrep_sidecar_bad_");
    FileGuard guard(path);
    LogLineStore store;
    REQUIRE(store.open(path).has_value());
    REQUIRE_FALSE(store.append(1, 1, "nope").has_value());
}

TEST_CASE("LogLineStore reports missing get on empty store", "[LogLineStore][core][Unit]")
{
    LogLineStore store;
    REQUIRE_FALSE(store.get(0).has_value());
    REQUIRE(store.get(0).error() == EngineError::DatabaseNotInitialized);
}

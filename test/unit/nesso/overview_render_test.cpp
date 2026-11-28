// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_all.hpp>
#include <nesso/overview.hpp>
#include <nesso_cli/render_json.hpp>
#include <nesso_cli/render_text.hpp>

#include <optional>
#include <string>

namespace
{

auto Totals() -> nesso::OverviewResults
{
    nesso::OverviewResults results;
    results.lines = 2000;
    results.types = 146;
    return results;
}

} // namespace

TEST_CASE("event type rows cite the first line and its count", "[overview][text]")
{
    const nesso::EventTypeRow line{.count = 3,
                                   .source = "app.log",
                                   .line = 12,
                                   .array_element = false,
                                   .text = "disk 3 failed",
                                   .key = "disk <*> failed",
                                   .first_seen = 7,
                                   .last_seen = 90};
    CHECK(nesso::cli::FormatEventTypeRow(line) == "app.log:line 12 (3): disk 3 failed");

    const nesso::EventTypeRow element{.count = 1,
                                      .source = "bundle.tar:var/rows.json",
                                      .line = 4,
                                      .array_element = true,
                                      .text = "quota: exceeded",
                                      .key = "quota: exceeded",
                                      .first_seen = 5,
                                      .last_seen = 5};
    CHECK(nesso::cli::FormatEventTypeRow(element) == "bundle.tar:var/rows.json:element 4 (1): quota: exceeded");

    const nesso::EventTypeRow piped{.count = 2,
                                    .source = "-",
                                    .line = 1,
                                    .array_element = false,
                                    .text = "x",
                                    .key = "x",
                                    .first_seen = 1,
                                    .last_seen = 2};
    CHECK(nesso::cli::FormatEventTypeRow(piped) == "-:line 1 (2): x");
}

TEST_CASE("event type rows escape the source and the text", "[overview][text]")
{
    const std::string text = "bell\x07 esc\x1B[2J c1\xC2\x9B bad\xFF ok\xC3\xA9";
    const nesso::EventTypeRow row{.count = 1,
                                  .source = "a\tb\xFE.log",
                                  .line = 1,
                                  .array_element = false,
                                  .text = text,
                                  .key = text,
                                  .first_seen = 1,
                                  .last_seen = 1};
    CHECK(nesso::cli::FormatEventTypeRow(row) ==
          "a\\x09b\\xFE.log:line 1 (1): bell\\x07 esc\\x1B[2J c1\\u009B bad\\xFF ok\xC3\xA9");
}

TEST_CASE("event type json keeps its key order and shares schema version 2", "[overview][json]")
{
    const nesso::OverviewResults results = Totals();
    const nesso::EventTypeRow line{.count = 3,
                                   .source = "app.log",
                                   .line = 12,
                                   .array_element = false,
                                   .text = "disk 3 failed",
                                   .key = "disk <*> failed",
                                   .first_seen = 7,
                                   .last_seen = 90};
    CHECK(
        nesso::cli::RenderEventTypeJson(results, line, 1) ==
        R"({"schema_version":2,"record":"event_type","rank":1,"count":3,"source":"app.log","kind":"line","line":12,"text":"disk 3 failed","key":"disk <*> failed","first_seen":7,"last_seen":90,"lines":2000,"types":146})");

    const nesso::EventTypeRow element{.count = 1,
                                      .source = "rows.json",
                                      .line = 4,
                                      .array_element = true,
                                      .text = "say \"hi\"\n",
                                      .key = "say \"hi\"\n",
                                      .first_seen = 2000,
                                      .last_seen = 2000};
    CHECK(
        nesso::cli::RenderEventTypeJson(results, element, 20) ==
        R"({"schema_version":2,"record":"event_type","rank":20,"count":1,"source":"rows.json","kind":"element","line":4,"text":"say \"hi\"\n","key":"say \"hi\"\n","first_seen":2000,"last_seen":2000,"lines":2000,"types":146})");
}

TEST_CASE("event type json replaces invalid utf-8", "[overview][json]")
{
    const nesso::OverviewResults results = Totals();
    const nesso::EventTypeRow row{.count = 1,
                                  .source = "a\xFF.log",
                                  .line = 1,
                                  .array_element = false,
                                  .text = "bad \xC3 end \xC3\xA9",
                                  .key = "key \xFE",
                                  .first_seen = 1,
                                  .last_seen = 1};
    CHECK(nesso::cli::RenderEventTypeJson(results, row, 1) ==
          "{\"schema_version\":2,\"record\":\"event_type\",\"rank\":1,\"count\":1,\"source\":\"a\xEF\xBF\xBD.log\","
          "\"kind\":\"line\",\"line\":1,\"text\":\"bad \xEF\xBF\xBD end \xC3\xA9\",\"key\":\"key \xEF\xBF\xBD\","
          "\"first_seen\":1,\"last_seen\":1,\"lines\":2000,\"types\":146}");
}

TEST_CASE("match json records keep no record key", "[overview][json]")
{
    const nesso::TextMatch match{.source = "app.log",
                                 .line = 2,
                                 .score = 0.5F,
                                 .text = "same",
                                 .array_element = false,
                                 .occurrences = 3,
                                 .mode = "dense",
                                 .dense = std::nullopt,
                                 .lexical = std::nullopt};
    CHECK(!nesso::cli::RenderMatchJson(match, 1).contains("\"record\""));
}

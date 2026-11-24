// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_all.hpp>
#include <parser/line_header.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

using nesso::parser::LineParts;
using nesso::parser::SplitLogLine;

// `header` followed by `message` is `line`, and both views point into it.
static void CheckLossless(std::string_view line, const LineParts &parts)
{
    REQUIRE(parts.header.size() + parts.message.size() == line.size());
    CHECK(parts.header.data() == line.data());
    CHECK(parts.message.data() == line.data() + parts.header.size());
    CHECK(parts.header.size() <= nesso::parser::MAX_HEADER_BYTES);
    if (!parts.header.empty())
    {
        CHECK(std::ranges::any_of(parts.message,
                                  [](char character) -> bool
                                  {
                                      const auto byte = static_cast<unsigned char>(character);
                                      return (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z') ||
                                             byte >= 0x80;
                                  }));
    }
}

static void ExpectSplit(std::string_view header, std::string_view message)
{
    const std::string line = std::string{header} + std::string{message};
    const LineParts parts = SplitLogLine(line);
    CheckLossless(line, parts);
    CHECK(parts.header == header);
    CHECK(parts.message == message);
}

static void ExpectWhole(std::string_view line)
{
    const LineParts parts = SplitLogLine(line);
    CheckLossless(line, parts);
    CHECK(parts.header.empty());
    CHECK(parts.message == line);
}

TEST_CASE("the rule version is the corpus policy value", "[line_header]")
{
    CHECK(nesso::parser::HEADER_RULE_VERSION == "message-v1");
}

TEST_CASE("iso 8601 timestamps start a header", "[line_header]")
{
    ExpectSplit("2026-11-23T10:00:00Z ", "payment timeout after 30s");
    ExpectSplit("2026-11-23 10:00:00 ", "payment timeout after 30s");
    ExpectSplit("2026-11-23T10:00:00.123456789+01:00 ", "cache miss");
    ExpectSplit("2026-11-23T10:00:00-0500 ", "cache miss");
    ExpectSplit("2026-11-23 10:00:00.123+01:00 ", "ERROR db.pool: connection refused");
    ExpectSplit("2015-10-18 18:01:47,978 ",
                "INFO [main] org.apache.hadoop.mapreduce.v2.app.MRAppMaster: Created MRAppMaster for application");
    ExpectSplit("2026-11-23 10:00:00,123 - ", "app.db - ERROR - connection pool exhausted");
    ExpectSplit("2026-11-23T10:00:00Z\t", "tab separated");
}

TEST_CASE("rfc 3164 timestamps take the host and the program tag", "[line_header]")
{
    ExpectSplit("Jun 14 15:16:01 combo sshd(pam_unix)[19939]: ", "authentication failure; logname= uid=0");
    ExpectSplit("Jun  9 06:06:20 combo kernel: ", "Linux version 2.6.5");
    ExpectSplit("Dec 10 06:55:46 LabSZ sshd[24200]: ", "reversed mapping checking getaddrinfo failed");
    ExpectSplit("<34>Oct 11 22:14:15 mymachine su: ", "'su root' failed for lonvick on /dev/pts/8");
    ExpectSplit("Jun 14 15:16:01 2005 combo cron[12]: ", "job started");
    // A level before the tag keeps both in the message.
    ExpectSplit("Jun 14 15:16:01 combo ", "ERROR cron[12]: job failed");
    // No tag: the host stays in the message.
    ExpectSplit("Jun  9 06:06:20 ", "combo syslogd 1.4.1: restart.");
}

TEST_CASE("rfc 5424 headers end after the host", "[line_header]")
{
    ExpectSplit("<34>1 2003-10-11T22:14:15.003Z mymachine.example.com ",
                "su - ID47 - 'su root' failed for lonvick on /dev/pts/8");
    ExpectSplit(
        "<165>1 2003-10-11T22:14:15.003Z host.example.com ",
        R"(evntslog - ID47 [exampleSDID@32473 iut="3" eventSource="App\]"][meta seq="1"] An application event)");
    ExpectSplit("<14>1 - host ", "app 12 - - no timestamp recorded");
    // Unclosed structured data: the generic rules see no anchor after `<14>`.
    ExpectWhole("<14>1 2003-10-11T22:14:15Z host app 12 ID [open data");
}

TEST_CASE("bracketed timestamps keep the level", "[line_header]")
{
    ExpectSplit("[Sun Dec 04 04:47:44 2005] ", "[notice] workerEnv.init() ok /etc/httpd/conf/workers2.properties");
    ExpectSplit("[Sun Dec 04 04:51:18 2005] ", "[error] mod_jk child workerEnv in error state 6");
    ExpectSplit("[2026-11-23T10:00:00Z] ", "[WARN] disk almost full");
    ExpectSplit("[23/Nov/2026:10:00:00 +0000] ", "GET /index.html took 3 ms");
    ExpectSplit("2026-11-23T10:00:00Z ", "<info> started");
    ExpectSplit("2026-11-23T10:00:00Z ", "Error: disk full");
    ExpectWhole("[not a time] something happened");
}

TEST_CASE("an epoch needs a date after it", "[line_header]")
{
    ExpectSplit("- 1117838570 2005.06.03 R02-M1-N0-C:J12-U11 2005-06-03-15.42.50.675872 R02-M1-N0-C:J12-U11 ",
                "RAS KERNEL INFO instruction cache parity error corrected");
    ExpectSplit("- 1131566461 2005.11.09 dn228 Nov 9 12:01:01 dn228/dn228 crond(pam_unix)[2915]: ",
                "session closed for user root");
    ExpectSplit("KERNDTLB 1131566461 2005.11.09 tbird-sm1 Nov 10 00:13:10 src@tbird-sm1 kernel: ",
                "data TLB error interrupt");
    ExpectSplit("1700000000123 2026-11-23 ", "millisecond epoch");
    ExpectWhole("1117838570 bytes were copied");
    ExpectWhole("- 1117838570 bytes were copied");
    ExpectWhole("x 1117838570 2005.06.03 lowercase label");
}

TEST_CASE("compact timestamps", "[line_header]")
{
    ExpectSplit("081109 203615 148 ",
                "INFO dfs.DataNode$PacketResponder: PacketResponder 1 for block blk_38865049064139660 terminating");
    ExpectSplit("20171223-22:15:29:606|Step_LSC|30002312|", "onStandStepChanged 3579");
    ExpectSplit("20171223-22:15:29.606 ", "dotted milliseconds");
    ExpectSplit("20171223-22:16:0:119|Step_LSC|30002312|", "processHandleBroadcastAction action:TIME_TICK");
    // A `|` after a noise field ends the header at the anchor.
    ExpectSplit("20171223-22:15:29 ", "4411 | Step_LSC|x");
    ExpectSplit("20171223-2:6:9 ", "unpadded clock");
    ExpectWhole("20171223-22:16:100 three digit seconds");
    ExpectSplit("20171223-22:15:29 | ", "pipe after a blank");
    ExpectWhole("081399 203615 148 INFO dfs.DataNode: month thirteen");
    ExpectWhole("123456 654321 widgets shipped");
}

TEST_CASE("a log file name may come before an iso timestamp", "[line_header]")
{
    ExpectSplit("app-server.log.1.2026-11-23_10:00:00 2026-11-23 10:00:00.008 25746 ",
                "INFO api.server.wsgi [req-1 - - -] GET /v2/servers status 200");
    ExpectSplit("/var/log/app.log 2026-11-23T10:00:00Z ", "rotated");
    ExpectWhole("server-1 2026-11-23 10:00:00 boot ok");
    ExpectWhole("app.log 2026-11-23 no time here");
}

TEST_CASE("noise fields join the header only before a field with meaning", "[line_header]")
{
    ExpectSplit("2026-11-23T10:00:00Z ", "3 retries failed");
    ExpectSplit("2026-11-23T10:00:00Z ", "JVM crashed");
    ExpectSplit("2026-11-23T10:00:00Z ", "node12 rebooted");
    ExpectSplit("2026-11-23T10:00:00Z node12 4411 ", "WARN fan speed low");
    ExpectSplit("2026-11-23T10:00:00Z 10:00:01 2026-11-23 2026.11.23 ", "INFO several clocks");
    ExpectSplit("2026-11-23T10:00:00Z ", "Connection reset: peer closed");
    ExpectSplit("2026-11-23T10:00:00Z [main] ", "app.Main plain message");
    ExpectSplit("2026-11-23T10:00:00Z [main] [req-7] ", "ERROR worker failed");
    ExpectSplit("2026-11-23T10:00:00Z ", "[main] plain message");
    ExpectSplit("Jun  9 06:06:20 ", "combo 12345");
}

TEST_CASE("lines without an anchor or without a message stay whole", "[line_header]")
{
    ExpectWhole("");
    ExpectWhole("   ");
    ExpectWhole("database connection refused");
    ExpectWhole("2026-11-23T10:00:00Z");
    ExpectWhole("2026-11-23T10:00:00Z ");
    ExpectSplit("2026-11-23T10:00:00Z ", "INFO");
    ExpectSplit("2026-11-23T10:00:00Z ", "INFO   ");
    ExpectWhole("Jun  9 06:06:20 12345 678");
    ExpectWhole("2026-11-23T10:00:00Z 42 -- 7");
    ExpectWhole(R"({"ts":"2026-11-23T10:00:00Z","msg":"x"})");
    ExpectWhole("time=2026-11-23T10:00:00Z level=error msg=x");
    ExpectWhole(" 2026-11-23T10:00:00Z leading blank");
    ExpectWhole("May the force be with you");
    ExpectWhole("2026 was a year");
    ExpectWhole("2026-13-01 10:00:00 month thirteen");
    ExpectWhole("2026-11-23T25:00:00Z hour 25");
    ExpectWhole("2026-11-23T10:00:00Zglued text");
    ExpectWhole("Jun 14 15:16:01");
    ExpectWhole("Jun 32 15:16:01 day 32");
    ExpectWhole("<1234>Jun 14 15:16:01 host app: long priority");
}

TEST_CASE("a noise field that ends the line stays in the message", "[line_header]")
{
    ExpectSplit("2026-11-23T10:00:00Z ", "node12 4411");
    ExpectSplit("2026-11-23T10:00:00Z ", "[main]");
    ExpectSplit("2026-11-23T10:00:00Z 4411 ", "app.db");
    ExpectSplit("1132144099 2005.11.16 R44-M1-N7-C:J02-U11 2005-11-16-04.28.19.392599 R44-M1-N7-C:J02-U11 ",
                "RAS KERNEL FATAL summary...........................1");
}

TEST_CASE("a header may be followed by json or utf-8", "[line_header]")
{
    ExpectSplit("2026-11-23T10:00:00Z app: ", R"({"msg":"payment declined"})");
    ExpectSplit("2026-11-23T10:00:00Z ", "\xc2\xa1"
                                         "fallo de disco!");
    ExpectSplit("2026-11-23T10:00:00Z ", "\xe2\x9c\x93\xe2\x9c\x93");
    ExpectSplit("2026-11-23T10:00:00Z ", "ok\r");
}

TEST_CASE("the header stops at the field and byte limits", "[line_header]")
{
    const std::string anchor = "2026-11-23T10:00:00Z ";
    // Seven noise fields before the level fit; nine exceed the eight-field limit.
    ExpectSplit(anchor + "1 2 3 4 5 6 7 ", "INFO message here");
    ExpectSplit(anchor, "1 2 3 4 5 6 7 8 9 INFO message here");
    // Three bracket fields pass 256 bytes before the level.
    const std::string field = "[" + std::string(100, 'x') + "]";
    ExpectSplit(anchor, field + " " + field + " " + field + " INFO too long");
    // A noise field may end at byte 255, so its blank is byte 256, but not at 256.
    const std::string two = field + " " + field + " ";
    const std::size_t room = nesso::parser::MAX_HEADER_BYTES - anchor.size() - two.size();
    const std::string fits = "[" + std::string(room - 3, 'z') + "]";
    REQUIRE(anchor.size() + two.size() + fits.size() + 1 == nesso::parser::MAX_HEADER_BYTES);
    ExpectSplit(anchor + two + fits + " ", "INFO message after the limit");
    const std::string full = "[" + std::string(room - 2, 'z') + "]";
    ExpectSplit(anchor, two + full + " INFO message after the limit");
    // A name over 96 bytes is not a tag, and a tag past the limit stays in the
    // message; the noise fields before it stay in the header.
    ExpectSplit(anchor, "1 2 x" + std::string(120, 't') + ": message after a long name");
    ExpectSplit(anchor + two, "x" + std::string(40, 't') + ": tag past the limit");
    // A bracket field over 128 bytes is not a field.
    ExpectSplit(anchor, "[" + std::string(140, 'y') + "] INFO long field");
}

TEST_CASE("every split is lossless on generated lines", "[line_header]")
{
    static constexpr std::array<std::string_view, 24> PIECES{"2026-11-23T10:00:00Z",
                                                             "Jun  9 06:06:20",
                                                             "<34>",
                                                             "1 ",
                                                             "[",
                                                             "]",
                                                             " ",
                                                             "\t",
                                                             "|",
                                                             " - ",
                                                             "INFO",
                                                             "sshd[42]:",
                                                             "host7",
                                                             "1117838570",
                                                             "2005.06.03",
                                                             "081109 203615",
                                                             ":",
                                                             "\xff",
                                                             "\x00",
                                                             "\r",
                                                             "msg",
                                                             "RAS",
                                                             "app.log",
                                                             "{\"a\":1}"};
    std::uint64_t state = 0x9e3779b97f4a7c15ULL;
    const auto next = [&state] -> std::uint64_t
    {
        state ^= state << 13U;
        state ^= state >> 7U;
        state ^= state << 17U;
        return state;
    };
    for (int round = 0; round < 10000; ++round)
    {
        std::string line;
        const std::size_t count = next() % 12;
        for (std::size_t piece = 0; piece < count; ++piece)
        {
            if (next() % 4 == 0)
            {
                line.push_back(static_cast<char>(next() & 0xFFU));
            }
            else
            {
                line += PIECES[next() % PIECES.size()];
            }
        }
        const LineParts parts = SplitLogLine(line);
        CheckLossless(line, parts);
        const LineParts again = SplitLogLine(line);
        CHECK(again.header.size() == parts.header.size());
    }
}

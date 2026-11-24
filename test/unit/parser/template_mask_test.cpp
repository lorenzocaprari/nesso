// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_all.hpp>
#include <parser/template_mask.hpp>

#include <clocale>
#include <string>
#include <string_view>

static auto Masked(std::string_view text) -> std::string { return nesso::parser::MaskTemplate(text); }

static void ExpectMasked(std::string_view text, std::string_view key)
{
    CHECK(Masked(text) == key);
    CHECK(Masked(std::string("head ") + std::string(text)) == std::string("head ") + std::string(key));
    CHECK(Masked(std::string(text) + " tail") == std::string(key) + " tail");
}

TEST_CASE("an empty line and a line with no variables stay", "[template_mask]")
{
    CHECK(Masked("").empty());
    CHECK(Masked("database connection refused") == "database connection refused");
    CHECK(Masked("<*>") == "<*>");
    CHECK(Masked(Masked("block blk_10 done")) == Masked("block blk_10 done"));
}

TEST_CASE("decimal numbers are masked at either end and when adjacent", "[template_mask]")
{
    ExpectMasked("42", "<*>");
    ExpectMasked("+42", "<*>");
    ExpectMasked("-7", "<*>");
    CHECK(Masked("a-7") == "<*>");
    CHECK(Masked("a -7") == "a <*>");
    CHECK(Masked("1,2") == "<*>,<*>");
    CHECK(Masked("1 2") == "<*> <*>");
    CHECK(Masked("+12x") == "+<*>");
}

TEST_CASE("tokens that mix letters and digits are masked", "[template_mask]")
{
    for (const char *word :
         {"http2", "sha256", "log4j", "30s", "b2b", "ipv4", "win32", "x86_64", "item2", "dn228", "126MB", "30000ms",
          "0x01", "0xabcde", "R02-M1-N0-C:J12-U11", "dn228/dn228", "attempt_1445144423722_0020_m_000002_1"})
    {
        ExpectMasked(word, "<*>");
    }
    CHECK(Masked("payment timeout after 30s") == "payment timeout after <*>");
    CHECK(Masked("node dn228: ready.") == "node <*>: ready.");
}

TEST_CASE("short words, words without digits, and dot leaders stay literal", "[template_mask]")
{
    for (const char *word : {"v2", "S3", "ab", "abcde", "deadline", "x-y", "a.b.c"})
    {
        CHECK(Masked(word) == word);
    }
    CHECK(Masked("data store interrupt caused by dcbf.........0") == "data store interrupt caused by dcbf.........<*>");
    CHECK(Masked("icache prefetch depth....................0") == "icache prefetch depth....................<*>");
}

TEST_CASE("node and host names no longer split one event into many keys", "[template_mask]")
{
    // BGL_2k and Thunderbird_2k lines: one event on different nodes and days.
    CHECK(Masked("- 1117838570 2005.06.03 R02-M1-N0-C:J12-U11 2005-06-03-15.42.50.675872 R02-M1-N0-C:J12-U11 RAS "
                 "KERNEL INFO instruction cache parity error corrected") ==
          Masked("- 1130840276 2005.11.01 R20-M1-NF-C:J10-U01 2005-11-01-02.17.56.853469 R20-M1-NF-C:J10-U01 RAS "
                 "KERNEL INFO instruction cache parity error corrected"));
    CHECK(Masked("- 1131566461 2005.11.09 dn228 Nov 9 12:01:01 dn228/dn228 crond(pam_unix)[2915]: session closed for "
                 "user root") ==
          Masked("- 1131566461 2005.11.09 dn261 Nov 9 12:01:01 dn261/dn261 crond(pam_unix)[2907]: session closed for "
                 "user root"));
}

TEST_CASE("hex runs of six or more are masked and a five character run is not", "[template_mask]")
{
    ExpectMasked("abcdef", "<*>");
    ExpectMasked("deadbeef", "<*>");
    ExpectMasked("0xdeadbeef", "<*>");
    ExpectMasked("0X123456", "<*>");
    CHECK(Masked("abcde") == "abcde");
    CHECK(Masked("dead") == "dead");
    CHECK(Masked("abcde-abcdef") == "abcde-<*>");
}

TEST_CASE("uuids are masked and a truncated uuid is not one token", "[template_mask]")
{
    const std::string id = "550e8400-e29b-41d4-a716-446655440000";
    ExpectMasked(id, "<*>");
    ExpectMasked("AAAAAAAA-BBBB-CCCC-DDDD-EEEEEEEEEEEE", "<*>");
    CHECK(Masked(id.substr(0, id.size() - 1)) != "<*>");
    CHECK(Masked("550e8400e29b41d4a716446655440000") == "<*>");
}

TEST_CASE("ipv4 addresses and optional ports are masked", "[template_mask]")
{
    ExpectMasked("10.0.0.1", "<*>");
    ExpectMasked("10.1.2.3:8080", "<*>");
    ExpectMasked("255.255.255.255:65535", "<*>");
    ExpectMasked("192.168.001.010", "<*>");
    CHECK(Masked("1.2.3") == "<*>.<*>.<*>");
    CHECK(Masked("1.2.3.4.5") == "<*>.<*>.<*>.<*>.<*>");
    CHECK(Masked("192.168.256.1") == "<*>.<*>.<*>.<*>");
    CHECK(Masked("10.0.0.1:65536") == "<*>.<*>.<*>.<*>:<*>");
    CHECK(Masked("10.0.0.1:") == "<*>.<*>.<*>.<*>:");
    CHECK(Masked("10.0.0.1abc") == "<*>.<*>.<*>.1abc");
}

TEST_CASE("ipv6 addresses and bracketed ports are masked", "[template_mask]")
{
    ExpectMasked("2001:db8::1", "<*>");
    ExpectMasked("::1", "<*>");
    ExpectMasked("1:2:3:4:5:6:7:8", "<*>");
    ExpectMasked("2001:0db8:85a3:0000:0000:8a2e:0370:7334", "<*>");
    ExpectMasked("::ffff:192.0.2.1", "<*>");
    ExpectMasked("[2001:db8::1]:443", "<*>");
    ExpectMasked("[::1]", "<*>");
    ExpectMasked("2001:db8::", "<*>");
    ExpectMasked("::abcd", "<*>");
    ExpectMasked("2001:db8::192.0.2.1", "<*>");
    CHECK(Masked("std::vector") == "std::vector");
    CHECK(Masked("foo::bar") == "foo::bar");
    CHECK(Masked("::") == "::");
    // Word lead-in rejects IPv6 at the digit; the mixed token stops at the
    // double colon and the trailing compressed form still matches.
    CHECK(Masked("x2001:db8::1") == "<*>::<*>");
    // A five-digit hextet breaks the address; the leading decimal and trailing
    // ::1 remain.
    CHECK(Masked("2001:db80a::1") == "<*>:db80a::<*>");
}

TEST_CASE("absolute paths that contain a digit are masked", "[template_mask]")
{
    ExpectMasked("/tmp/file1.txt", "<*>");
    ExpectMasked("/data/node1/blk_9", "<*>");
    CHECK(Masked("/var/log/app.log") == "/var/log/app.log");
    CHECK(Masked("foo/bar2") == "<*>");
    CHECK(Masked("~/file1") == "~/file1");
    CHECK(Masked("/var/log/app.log:123") == "/var/log/app.log:<*>");
}

TEST_CASE("hdfs block ids include a leading minus", "[template_mask]")
{
    ExpectMasked("blk_38865049064139660", "<*>");
    ExpectMasked("blk_-6952295868487656571", "<*>");
    ExpectMasked("blk_1", "<*>");
    CHECK(Masked("blk_") == "blk_");
    CHECK(Masked("blk_abc") == "blk_abc");
    CHECK(Masked("BLK_123") == "<*>");
    CHECK(Masked("myblk_123") == "<*>");
    CHECK(Masked("blk_1blk_2") == "<*><*>");
    CHECK(Masked("blk_1abc") == "<*>");
}

TEST_CASE("timestamps are masked and near-miss clocks stay split", "[template_mask]")
{
    ExpectMasked("2020-01-02", "<*>");
    ExpectMasked("2020-01-02T03:04:05", "<*>");
    ExpectMasked("2020-01-02t03:04:05.123Z", "<*>");
    ExpectMasked("2020-01-02T03:04:05+00:00", "<*>");
    ExpectMasked("2020-01-02T03:04:05+0000", "<*>");
    ExpectMasked("2020-01-02 03:04:05", "<*>");
    ExpectMasked("Jan  2 03:04:05", "<*>");
    ExpectMasked("Jan 02 03:04:05", "<*>");
    ExpectMasked("03:04:05", "<*>");
    ExpectMasked("12:34:56.789", "<*>");
    CHECK(Masked("2020-13-01") == "<*>-<*>-<*>");
    CHECK(Masked("12:34") == "<*>:<*>");
    CHECK(Masked("25:00:00") == "<*>:<*>:<*>");
    CHECK(Masked("January 2 03:04:05") == "January <*> <*>");
    CHECK(Masked("Mon 02 03:04:05") == "Mon <*> <*>");
    CHECK(Masked("Jan 32 00:00:00") == "Jan <*> <*>");
}

TEST_CASE("rejected tokens fall through to a shorter class", "[template_mask]")
{
    CHECK(Masked("1234.1.1.1") == "<*>.<*>.<*>.<*>");
    CHECK(Masked("10.0.0.1:123456") == "<*>.<*>.<*>.<*>:<*>");
    CHECK(Masked("[::1]:99999") == "[<*>]:<*>");
    CHECK(Masked("[::1") == "[<*>");
    CHECK(Masked(":::1") == ":<*>");
    CHECK(Masked("[2001:db8::1]:") == "[<*>]:");
    CHECK(Masked("2020-01-02T") == "<*>-<*>-02T");
    CHECK(Masked("2020-01-02T25:00:00") == "<*>-<*>-02T25:<*>:<*>");
    CHECK(Masked("x2020-01-02") == "<*>");
    CHECK(Masked("2020-01-02Zulu") == "<*>-<*>-02Zulu");
    CHECK(Masked("2020-00-01") == "<*>-<*>-<*>");
    CHECK(Masked("2020-01-00") == "<*>-<*>-<*>");
    CHECK(Masked("2020-01-32") == "<*>-<*>-<*>");
    CHECK(Masked("0x") == "0x");
    CHECK(Masked("+") == "+");
    CHECK(Masked("/") == "/");
    CHECK(Masked("Jan") == "Jan");
    CHECK(Masked("blk_-") == "blk_-");
    CHECK(Masked("DEADBEEF") == "<*>");
    CHECK(Masked("fe80::1%eth0") == "<*>%<*>");
    // Eight groups is an address, so the leading number falls off and the rest
    // matches.
    CHECK(Masked("1:2:3:4:5:6:7:8:9") == "<*>:<*>");
    CHECK(Masked("ggggg::1") == "ggggg::<*>");
    CHECK(Masked("12:34:567") == "<*>:<*>:<*>");
    CHECK(Masked("112:34:56") == "<*>:<*>:<*>");
    CHECK(Masked("03:04:05.12345678901") == "<*>.<*>");
    CHECK(Masked("Jan  0 00:00:00") == "Jan  <*> <*>");
    CHECK(Masked("0.0.0.0:0") == "<*>");
    CHECK(Masked("::ffff:999.1.1.1") == "::ffff:<*>.<*>.<*>.<*>");
}

TEST_CASE("mixed tokens on one line collapse to one key", "[template_mask]")
{
    const std::string first = "PacketResponder 1 for block blk_38865049064139660 terminating";
    const std::string second = "PacketResponder 0 for block blk_-6952295868487656571 terminating";
    CHECK(Masked(first) == Masked(second));
    CHECK(Masked(first) == "PacketResponder <*> for block <*> terminating");
    CHECK(Masked("id 550e8400-e29b-41d4-a716-446655440000 at 10.0.0.1:80") == "id <*> at <*>");
}

TEST_CASE("utf-8 bytes and long lines stay deterministic", "[template_mask]")
{
    const std::string cafe = "caf\u00e9 12";
    CHECK(Masked(cafe) == "caf\u00e9 <*>");
    const std::string digits(10000, '9');
    CHECK(Masked(digits) == "<*>");
    std::string line(8192, 'g');
    line.replace(4000, 3, " 7 ");
    const std::string key = Masked(line);
    CHECK(key == std::string(4000, 'g') + " <*> " + std::string(4189, 'g'));
    CHECK(Masked(line) == key);
    const char *previous = std::setlocale(LC_ALL, nullptr);
    CHECK(std::setlocale(LC_ALL, "C") != nullptr);
    CHECK(Masked(line) == key);
    CHECK(Masked("Jan  2 03:04:05 blk_-12 10.0.0.1") == "<*> <*> <*>");
    if (previous != nullptr)
    {
        CHECK(std::setlocale(LC_ALL, previous) != nullptr);
    }
}

// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_all.hpp>
#include <parser/template_mask.hpp>

#include <clocale>
#include <string>
#include <string_view>

static std::string masked(std::string_view text) { return nesso::parser::maskTemplate(text); }

static void expectMasked(std::string_view text, std::string_view key)
{
    CHECK(masked(text) == key);
    CHECK(masked(std::string("head ") + std::string(text)) == std::string("head ") + std::string(key));
    CHECK(masked(std::string(text) + " tail") == std::string(key) + " tail");
}

TEST_CASE("an empty line and a line with no variables stay", "[template_mask]")
{
    CHECK(masked("").empty());
    CHECK(masked("database connection refused") == "database connection refused");
    CHECK(masked("<*>") == "<*>");
    CHECK(masked(masked("block blk_10 done")) == masked("block blk_10 done"));
}

TEST_CASE("decimal numbers are masked at either end and when adjacent", "[template_mask]")
{
    expectMasked("42", "<*>");
    expectMasked("+42", "<*>");
    expectMasked("-7", "<*>");
    CHECK(masked("a-7") == "a-<*>");
    CHECK(masked("1,2") == "<*>,<*>");
    CHECK(masked("1 2") == "<*> <*>");
    CHECK(masked("+12x") == "+12x");
}

TEST_CASE("words that contain digits stay literal", "[template_mask]")
{
    for (const char *word : {"http2", "sha256", "log4j", "30s", "b2b", "ipv4", "win32", "x86_64", "item2"})
    {
        CHECK(masked(word) == word);
    }
    CHECK(masked("0xabcde") == "0xabcde");
    CHECK(masked("payment timeout after 30s") == "payment timeout after 30s");
}

TEST_CASE("hex runs of six or more are masked and a five character run is not", "[template_mask]")
{
    expectMasked("abcdef", "<*>");
    expectMasked("deadbeef", "<*>");
    expectMasked("0xdeadbeef", "<*>");
    expectMasked("0X123456", "<*>");
    CHECK(masked("abcde") == "abcde");
    CHECK(masked("dead") == "dead");
    CHECK(masked("abcde-abcdef") == "abcde-<*>");
}

TEST_CASE("uuids are masked and a truncated uuid is not one token", "[template_mask]")
{
    const std::string id = "550e8400-e29b-41d4-a716-446655440000";
    expectMasked(id, "<*>");
    expectMasked("AAAAAAAA-BBBB-CCCC-DDDD-EEEEEEEEEEEE", "<*>");
    CHECK(masked(id.substr(0, id.size() - 1)) != "<*>");
    CHECK(masked("550e8400e29b41d4a716446655440000") == "<*>");
}

TEST_CASE("ipv4 addresses and optional ports are masked", "[template_mask]")
{
    expectMasked("10.0.0.1", "<*>");
    expectMasked("10.1.2.3:8080", "<*>");
    expectMasked("255.255.255.255:65535", "<*>");
    expectMasked("192.168.001.010", "<*>");
    CHECK(masked("1.2.3") == "<*>.<*>.<*>");
    CHECK(masked("1.2.3.4.5") == "<*>.<*>.<*>.<*>.<*>");
    CHECK(masked("192.168.256.1") == "<*>.<*>.<*>.<*>");
    CHECK(masked("10.0.0.1:65536") == "<*>.<*>.<*>.<*>:<*>");
    CHECK(masked("10.0.0.1:") == "<*>.<*>.<*>.<*>:");
    CHECK(masked("10.0.0.1abc") == "<*>.<*>.<*>.1abc");
}

TEST_CASE("ipv6 addresses and bracketed ports are masked", "[template_mask]")
{
    expectMasked("2001:db8::1", "<*>");
    expectMasked("::1", "<*>");
    expectMasked("1:2:3:4:5:6:7:8", "<*>");
    expectMasked("2001:0db8:85a3:0000:0000:8a2e:0370:7334", "<*>");
    expectMasked("::ffff:192.0.2.1", "<*>");
    expectMasked("[2001:db8::1]:443", "<*>");
    expectMasked("[::1]", "<*>");
    CHECK(masked("std::vector") == "std::vector");
    CHECK(masked("foo::bar") == "foo::bar");
    CHECK(masked("::") == "::");
}

TEST_CASE("absolute paths that contain a digit are masked", "[template_mask]")
{
    expectMasked("/tmp/file1.txt", "<*>");
    expectMasked("/data/node1/blk_9", "<*>");
    CHECK(masked("/var/log/app.log") == "/var/log/app.log");
    CHECK(masked("foo/bar2") == "foo/bar2");
    CHECK(masked("~/file1") == "~/file1");
    CHECK(masked("/var/log/app.log:123") == "/var/log/app.log:<*>");
}

TEST_CASE("hdfs block ids include a leading minus", "[template_mask]")
{
    expectMasked("blk_38865049064139660", "<*>");
    expectMasked("blk_-6952295868487656571", "<*>");
    expectMasked("blk_1", "<*>");
    CHECK(masked("blk_") == "blk_");
    CHECK(masked("blk_abc") == "blk_abc");
    CHECK(masked("BLK_123") == "BLK_123");
    CHECK(masked("myblk_123") == "myblk_123");
    CHECK(masked("blk_1blk_2") == "<*><*>");
    CHECK(masked("blk_1abc") == "blk_1abc");
}

TEST_CASE("timestamps are masked and near-miss clocks stay split", "[template_mask]")
{
    expectMasked("2020-01-02", "<*>");
    expectMasked("2020-01-02T03:04:05", "<*>");
    expectMasked("2020-01-02t03:04:05.123Z", "<*>");
    expectMasked("2020-01-02T03:04:05+00:00", "<*>");
    expectMasked("2020-01-02T03:04:05+0000", "<*>");
    expectMasked("2020-01-02 03:04:05", "<*>");
    expectMasked("Jan  2 03:04:05", "<*>");
    expectMasked("Jan 02 03:04:05", "<*>");
    expectMasked("03:04:05", "<*>");
    expectMasked("12:34:56.789", "<*>");
    CHECK(masked("2020-13-01") == "<*>-<*>-<*>");
    CHECK(masked("12:34") == "<*>:<*>");
    CHECK(masked("25:00:00") == "<*>:<*>:<*>");
    CHECK(masked("January 2 03:04:05") == "January <*> <*>");
    CHECK(masked("Mon 02 03:04:05") == "Mon <*> <*>");
    CHECK(masked("Jan 32 00:00:00") == "Jan <*> <*>");
}

TEST_CASE("rejected tokens fall through to a shorter class", "[template_mask]")
{
    CHECK(masked("1234.1.1.1") == "<*>.<*>.<*>.<*>");
    CHECK(masked("10.0.0.1:123456") == "<*>.<*>.<*>.<*>:<*>");
    CHECK(masked("[::1]:99999") == "[<*>]:<*>");
    CHECK(masked("[::1") == "[<*>");
    CHECK(masked(":::1") == ":<*>");
    CHECK(masked("[2001:db8::1]:") == "[<*>]:");
    CHECK(masked("2020-01-02T") == "<*>-<*>-02T");
    CHECK(masked("2020-01-02T25:00:00") == "<*>-<*>-02T25:<*>:<*>");
    CHECK(masked("x2020-01-02") == "x2020-<*>-<*>");
    CHECK(masked("2020-01-02Zulu") == "<*>-<*>-02Zulu");
    CHECK(masked("2020-00-01") == "<*>-<*>-<*>");
    CHECK(masked("2020-01-00") == "<*>-<*>-<*>");
    CHECK(masked("2020-01-32") == "<*>-<*>-<*>");
    CHECK(masked("0x") == "0x");
    CHECK(masked("+") == "+");
    CHECK(masked("/") == "/");
    CHECK(masked("Jan") == "Jan");
    CHECK(masked("blk_-") == "blk_-");
    CHECK(masked("DEADBEEF") == "<*>");
    CHECK(masked("fe80::1%eth0") == "<*>%eth0");
    // Eight groups is an address, so the leading number falls off and the rest matches.
    CHECK(masked("1:2:3:4:5:6:7:8:9") == "<*>:<*>");
    CHECK(masked("ggggg::1") == "ggggg::<*>");
    CHECK(masked("12:34:567") == "<*>:<*>:<*>");
    CHECK(masked("112:34:56") == "<*>:<*>:<*>");
    CHECK(masked("03:04:05.12345678901") == "<*>.<*>");
    CHECK(masked("Jan  0 00:00:00") == "Jan  <*> <*>");
    CHECK(masked("0.0.0.0:0") == "<*>");
    CHECK(masked("::ffff:999.1.1.1") == "::ffff:<*>.<*>.<*>.<*>");
}

TEST_CASE("mixed tokens on one line collapse to one key", "[template_mask]")
{
    const std::string first = "PacketResponder 1 for block blk_38865049064139660 terminating";
    const std::string second = "PacketResponder 0 for block blk_-6952295868487656571 terminating";
    CHECK(masked(first) == masked(second));
    CHECK(masked(first) == "PacketResponder <*> for block <*> terminating");
    CHECK(masked("id 550e8400-e29b-41d4-a716-446655440000 at 10.0.0.1:80") == "id <*> at <*>");
}

TEST_CASE("utf-8 bytes and long lines stay deterministic", "[template_mask]")
{
    const std::string cafe = "caf\u00e9 12";
    CHECK(masked(cafe) == "caf\u00e9 <*>");
    const std::string digits(10000, '9');
    CHECK(masked(digits) == "<*>");
    std::string line(8192, 'g');
    line.replace(4000, 3, " 7 ");
    const std::string key = masked(line);
    CHECK(key == std::string(4000, 'g') + " <*> " + std::string(4189, 'g'));
    CHECK(masked(line) == key);
    const char *previous = std::setlocale(LC_ALL, nullptr);
    CHECK(std::setlocale(LC_ALL, "C") != nullptr);
    CHECK(masked(line) == key);
    CHECK(masked("Jan  2 03:04:05 blk_-12 10.0.0.1") == "<*> <*> <*>");
    if (previous != nullptr)
    {
        CHECK(std::setlocale(LC_ALL, previous) != nullptr);
    }
}

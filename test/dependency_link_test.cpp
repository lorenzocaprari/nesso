// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#include <catch2/catch_all.hpp>
#include <nlohmann/json.hpp>
#include <onnxruntime_cxx_api.h>

TEST_CASE("nlohmann_json parses a trivial object", "[deps][Unit]")
{
    const auto document = nlohmann::json::parse(R"({"ok":true,"n":1})");

    REQUIRE(document.at("ok").get<bool>());
    REQUIRE(document.at("n").get<int>() == 1);
}

TEST_CASE("onnxruntime constructs an Ort::Env", "[deps][Unit]")
{
    REQUIRE_NOTHROW(Ort::Env{ORT_LOGGING_LEVEL_WARNING, "mach1-deps"});
}

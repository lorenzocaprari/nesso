#!/usr/bin/env python3
"""Generate explicit test/CMakeLists.txt for the current tree (no add_catch_test)."""

from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TEST_DIR = ROOT / "test"


def has_test(name: str) -> bool:
    return (TEST_DIR / f"{name}.cpp").exists()


def main() -> None:
    lines = [
        "find_package(Catch2 CONFIG REQUIRED)",
        "find_package(nlohmann_json CONFIG REQUIRED)",
        "find_package(onnxruntime CONFIG REQUIRED)",
        "",
        'set(MACH1_TEST_FIXTURES "${CMAKE_SOURCE_DIR}/test/fixtures")',
        "",
        "add_executable(dependency_link_test dependency_link_test.cpp)",
        "add_test(NAME dependency_link_test COMMAND dependency_link_test)",
        "target_link_libraries(",
        "  dependency_link_test",
        "  PRIVATE Catch2::Catch2WithMain nlohmann_json::nlohmann_json",
        "          onnxruntime::onnxruntime)",
        "target_compile_options(dependency_link_test PRIVATE -fno-profile-arcs",
        "                                                    -fno-test-coverage)",
        "",
        "add_executable(storage_engine_test storage_engine_test.cpp)",
        "add_test(NAME storage_engine_test COMMAND storage_engine_test)",
        "target_link_libraries(storage_engine_test PRIVATE mach_core",
        "                                                  Catch2::Catch2WithMain)",
        "target_include_directories(storage_engine_test",
        "                           PRIVATE \"${CMAKE_CURRENT_SOURCE_DIR}/include\")",
        "",
    ]

    if has_test("distance_test"):
        lines.extend(
            [
                "add_executable(distance_test distance_test.cpp)",
                "add_test(NAME distance_test COMMAND distance_test)",
                "target_link_libraries(distance_test PRIVATE mach_core Catch2::Catch2WithMain)",
                "target_include_directories(distance_test",
                "                           PRIVATE \"${CMAKE_CURRENT_SOURCE_DIR}/include\")",
                "target_compile_definitions(distance_test PRIVATE MACH1_DISTANCE_DETAIL_TEST=1)",
                'if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")',
                "  target_compile_options(distance_test PRIVATE -mavx2)",
                "endif()",
                "",
            ]
        )

    lines.extend(
        [
            "add_executable(vector_search_test vector_search_test.cpp)",
            "add_test(NAME vector_search_test COMMAND vector_search_test)",
            "target_link_libraries(vector_search_test PRIVATE mach_core",
            "                                                 Catch2::Catch2WithMain)",
            "target_include_directories(vector_search_test",
            "                           PRIVATE \"${CMAKE_CURRENT_SOURCE_DIR}/include\")",
            "",
        ]
    )

    if has_test("vector_index_test"):
        lines.extend(
            [
                "add_executable(vector_index_test vector_index_test.cpp)",
                "add_test(NAME vector_index_test COMMAND vector_index_test)",
                "target_link_libraries(vector_index_test PRIVATE mach_core",
                "                                                Catch2::Catch2WithMain)",
                "",
            ]
        )

    lines.extend(
        [
            "add_executable(search_oracle_test search_oracle_test.cpp)",
            "add_test(NAME search_oracle_test COMMAND search_oracle_test)",
            "target_link_libraries(search_oracle_test PRIVATE mach_core",
            "                                                 Catch2::Catch2WithMain)",
            "",
        ]
    )

    if has_test("wordpiece_tokenizer_test"):
        lines.extend(
            [
                "add_executable(wordpiece_tokenizer_test wordpiece_tokenizer_test.cpp)",
                "add_test(NAME wordpiece_tokenizer_test COMMAND wordpiece_tokenizer_test)",
                "target_link_libraries(wordpiece_tokenizer_test PRIVATE mach_embed",
                "                                                       Catch2::Catch2WithMain)",
                "target_compile_definitions(wordpiece_tokenizer_test",
                '                           PRIVATE MACH1_TEST_FIXTURES="${MACH1_TEST_FIXTURES}")',
                "",
            ]
        )

    if has_test("log_chunker_test"):
        lines.extend(
            [
                "add_executable(log_chunker_test log_chunker_test.cpp)",
                "add_test(NAME log_chunker_test COMMAND log_chunker_test)",
                "target_link_libraries(log_chunker_test PRIVATE mach_parser",
                "                                               Catch2::Catch2WithMain)",
                "target_compile_definitions(log_chunker_test",
                '                           PRIVATE MACH1_TEST_FIXTURES="${MACH1_TEST_FIXTURES}")',
                "",
            ]
        )

    if has_test("onnx_embedder_test"):
        lines.extend(
            [
                "add_executable(onnx_embedder_test onnx_embedder_test.cpp)",
                "target_link_libraries(onnx_embedder_test PRIVATE mach_embed",
                "                                                 Catch2::Catch2WithMain)",
                "target_compile_definitions(onnx_embedder_test",
                '                           PRIVATE MACH1_TEST_FIXTURES="${MACH1_TEST_FIXTURES}")',
                "target_compile_options(onnx_embedder_test PRIVATE -fno-profile-arcs",
                "                                                  -fno-test-coverage)",
                "add_test(NAME onnx_embedder_test COMMAND $<TARGET_FILE:onnx_embedder_test>",
                "                                         --allow-running-no-tests)",
                "",
            ]
        )

    if has_test("grep_integration_test"):
        lines.extend(
            [
                "add_executable(grep_integration_test grep_integration_test.cpp)",
                "target_link_libraries(",
                "  grep_integration_test PRIVATE mach_core mach_embed mach_parser",
                "                                Catch2::Catch2WithMain)",
                "target_compile_definitions(grep_integration_test",
                '                           PRIVATE MACH1_TEST_FIXTURES="${MACH1_TEST_FIXTURES}")',
                "target_compile_options(grep_integration_test PRIVATE -fno-profile-arcs",
                "                                                     -fno-test-coverage)",
                "add_test(NAME grep_integration_test COMMAND $<TARGET_FILE:grep_integration_test>",
                "                                            --allow-running-no-tests)",
                "",
            ]
        )

    lines.extend(
        [
            "add_executable(double_search_test double_search_test.cpp)",
            "add_test(NAME double_search_test COMMAND double_search_test)",
            "target_link_libraries(double_search_test PRIVATE mach_core)",
            "target_compile_options(double_search_test PRIVATE -fno-profile-arcs",
            "                                                  -fno-test-coverage)",
            "",
        ]
    )

    out = TEST_DIR / "CMakeLists.txt"
    out.write_text("\n".join(lines), encoding="utf-8")


if __name__ == "__main__":
    main()

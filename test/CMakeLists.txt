find_package(Catch2 CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(onnxruntime CONFIG REQUIRED)

file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
     DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")
set(NESSO_TEST_FIXTURES "${CMAKE_CURRENT_BINARY_DIR}/fixtures")
set(NESSO_MODELS_DIR "${CMAKE_SOURCE_DIR}/models")

# NESSO_REQUIRE_MODEL=1 at configure time fails model-backed tests instead of
# skipping them, and drops Catch2's --allow-running-no-tests escape hatch.
if(NOT DEFINED NESSO_REQUIRE_MODEL)
  if("$ENV{NESSO_REQUIRE_MODEL}" MATCHES "^(1|ON|on|TRUE|true)$")
    set(NESSO_REQUIRE_MODEL ON)
  else()
    set(NESSO_REQUIRE_MODEL OFF)
  endif()
endif()
set(NESSO_MODEL_TEST_DEFS "NESSO_MODELS_DIR=\"${NESSO_MODELS_DIR}\"")
set(NESSO_MODEL_TEST_ARGS)
if(NESSO_REQUIRE_MODEL)
  list(APPEND NESSO_MODEL_TEST_DEFS NESSO_REQUIRE_MODEL=1)
else()
  set(NESSO_MODEL_TEST_ARGS --allow-running-no-tests)
endif()

add_subdirectory(fuzz)

add_executable(dependency_link_test dependency_link_test.cpp)
add_test(NAME dependency_link_test COMMAND dependency_link_test)
target_link_libraries(
  dependency_link_test
  PRIVATE Catch2::Catch2WithMain nlohmann_json::nlohmann_json
          onnxruntime::onnxruntime)
# Linkage smoke test only — keep out of the coverage gate.
target_compile_options(dependency_link_test PRIVATE -fno-profile-arcs
                                                    -fno-test-coverage)

add_executable(storage_engine_test storage_engine_test.cpp)
add_test(NAME storage_engine_test COMMAND storage_engine_test)
target_link_libraries(storage_engine_test PRIVATE core Catch2::Catch2WithMain)
target_include_directories(storage_engine_test
                           PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")

add_executable(distance_test distance_test.cpp)
add_test(NAME distance_test COMMAND distance_test)
target_link_libraries(distance_test PRIVATE core Catch2::Catch2WithMain)
target_include_directories(distance_test
                           PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_definitions(distance_test PRIVATE NESSO_DISTANCE_DETAIL_TEST=1)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
  target_compile_options(distance_test PRIVATE -mavx2)
endif()

add_executable(vector_search_test vector_search_test.cpp)
add_test(NAME vector_search_test COMMAND vector_search_test)
target_link_libraries(vector_search_test PRIVATE core Catch2::Catch2WithMain)
target_include_directories(vector_search_test
                           PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")

add_executable(corpus_index_test corpus_index_test.cpp)
add_test(NAME corpus_index_test COMMAND corpus_index_test)
target_link_libraries(corpus_index_test PRIVATE core Catch2::Catch2WithMain)

add_executable(embedding_store_test embedding_store_test.cpp)
add_test(NAME embedding_store_test COMMAND embedding_store_test)
target_link_libraries(embedding_store_test PRIVATE core Catch2::Catch2WithMain)

add_executable(search_oracle_test search_oracle_test.cpp)
add_test(NAME search_oracle_test COMMAND search_oracle_test)
target_link_libraries(search_oracle_test PRIVATE core Catch2::Catch2WithMain)

add_executable(wordpiece_tokenizer_test wordpiece_tokenizer_test.cpp)
add_test(NAME wordpiece_tokenizer_test COMMAND wordpiece_tokenizer_test)
target_link_libraries(wordpiece_tokenizer_test PRIVATE tokenizer
                                                       Catch2::Catch2WithMain)
target_compile_definitions(wordpiece_tokenizer_test
                           PRIVATE NESSO_TEST_FIXTURES="${NESSO_TEST_FIXTURES}")

add_executable(log_chunker_test log_chunker_test.cpp)
add_test(NAME log_chunker_test COMMAND log_chunker_test)
target_link_libraries(log_chunker_test PRIVATE parser Catch2::Catch2WithMain)
target_compile_definitions(log_chunker_test
                           PRIVATE NESSO_TEST_FIXTURES="${NESSO_TEST_FIXTURES}")
# Harness stays out of the coverage floor. LogChunker is still measured through
# the instrumented parser library.
target_compile_options(log_chunker_test PRIVATE -fno-profile-arcs
                                                -fno-test-coverage)

add_executable(onnx_embedder_test onnx_embedder_test.cpp)
target_link_libraries(onnx_embedder_test PRIVATE embed Catch2::Catch2WithMain)
target_compile_definitions(onnx_embedder_test PRIVATE ${NESSO_MODEL_TEST_DEFS})
# Skips when models/ is absent. An instrumented skip would record no hits and
# fail the floor.
target_compile_options(onnx_embedder_test PRIVATE -fno-profile-arcs
                                                  -fno-test-coverage)
add_test(NAME onnx_embedder_test COMMAND $<TARGET_FILE:onnx_embedder_test>
                                         ${NESSO_MODEL_TEST_ARGS})

add_executable(grep_integration_test grep_integration_test.cpp)
target_link_libraries(grep_integration_test PRIVATE core embed parser
                                                    Catch2::Catch2WithMain)
target_compile_definitions(
  grep_integration_test PRIVATE NESSO_TEST_FIXTURES="${NESSO_TEST_FIXTURES}"
                                ${NESSO_MODEL_TEST_DEFS})
# Skips when models/ is absent. An instrumented skip would record no hits and
# fail the floor.
target_compile_options(grep_integration_test PRIVATE -fno-profile-arcs
                                                     -fno-test-coverage)
add_test(NAME grep_integration_test COMMAND $<TARGET_FILE:grep_integration_test>
                                            ${NESSO_MODEL_TEST_ARGS})

add_executable(cli_store_test cli_store_test.cpp)
add_test(NAME cli_store_test COMMAND cli_store_test)
target_link_libraries(cli_store_test PRIVATE Catch2::Catch2WithMain)
target_compile_definitions(
  cli_store_test
  PRIVATE "NESSO_BINARY=\"$<TARGET_FILE:nesso>\""
          "NESSO_TEST_FIXTURES=\"${NESSO_TEST_FIXTURES}\""
          "NESSO_MODELS_DIR=\"${NESSO_MODELS_DIR}\"")
# This test forks and execs nesso. The child never flushes gcov, so
# instrumenting this TU records only the parent and pulls the library coverage
# floor down.
target_compile_options(cli_store_test PRIVATE -fno-profile-arcs
                                              -fno-test-coverage)
add_dependencies(cli_store_test nesso)

add_executable(model_paths_test model_paths_test.cpp)
add_test(NAME model_paths_test COMMAND model_paths_test)
target_link_libraries(model_paths_test PRIVATE nesso_lib Catch2::Catch2WithMain)

add_executable(nesso_facade_test nesso_facade_test.cpp)
add_test(NAME nesso_facade_test COMMAND nesso_facade_test)
target_link_libraries(nesso_facade_test PRIVATE nesso_lib
                                                Catch2::Catch2WithMain)
target_compile_definitions(nesso_facade_test
                           PRIVATE NESSO_TEST_FIXTURES="${NESSO_TEST_FIXTURES}")

add_executable(double_search_test double_search_test.cpp)
add_test(NAME double_search_test COMMAND double_search_test)
target_link_libraries(double_search_test PRIVATE core)
# Exercise the double instantiations without adding test-harness branches to the
# coverage gate.
target_compile_options(double_search_test PRIVATE -fno-profile-arcs
                                                  -fno-test-coverage)

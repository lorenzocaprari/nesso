add_executable(dependency_link_test dependency_link_test.cpp)
add_test(NAME dependency_link_test COMMAND dependency_link_test)
target_link_libraries(
  dependency_link_test
  PRIVATE Catch2::Catch2WithMain nlohmann_json::nlohmann_json
          onnxruntime::onnxruntime)
# Linkage smoke test only — keep out of the coverage gate.
target_compile_options(dependency_link_test PRIVATE -fno-profile-arcs
                                                    -fno-test-coverage)

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

set_tests_properties(dependency_link_test grep_integration_test cli_store_test
                     PROPERTIES LABELS integration)

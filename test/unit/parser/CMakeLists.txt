add_executable(log_chunker_test log_chunker_test.cpp)
add_test(NAME log_chunker_test COMMAND log_chunker_test)
target_link_libraries(log_chunker_test PRIVATE parser Catch2::Catch2WithMain)
target_compile_definitions(log_chunker_test
                           PRIVATE NESSO_TEST_FIXTURES="${NESSO_TEST_FIXTURES}")
# Harness stays out of the coverage floor. LogChunker is still measured through
# the instrumented parser library.
target_compile_options(log_chunker_test PRIVATE -fno-profile-arcs
                                                -fno-test-coverage)
set_tests_properties(log_chunker_test PROPERTIES LABELS unit)

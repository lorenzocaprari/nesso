add_executable(model_paths_test model_paths_test.cpp)
add_test(NAME model_paths_test COMMAND model_paths_test)
target_link_libraries(model_paths_test PRIVATE nesso_lib Catch2::Catch2WithMain)

add_executable(nesso_facade_test nesso_facade_test.cpp)
add_test(NAME nesso_facade_test COMMAND nesso_facade_test)
target_link_libraries(nesso_facade_test PRIVATE nesso_lib
                                                Catch2::Catch2WithMain)
target_compile_definitions(
  nesso_facade_test PRIVATE NESSO_TEST_FIXTURES="${NESSO_TEST_FIXTURES}"
                            ${NESSO_MODEL_TEST_DEFS})

add_executable(trace_test trace_test.cpp)
add_test(NAME trace_test COMMAND trace_test)
target_link_libraries(trace_test PRIVATE nesso_lib Catch2::Catch2WithMain)
# The harness only redirects stderr. trace.cpp stays in the coverage floor.
target_compile_options(trace_test PRIVATE -fno-profile-arcs -fno-test-coverage)

set_tests_properties(model_paths_test nesso_facade_test trace_test
                     PROPERTIES LABELS unit)

add_executable(model_paths_test model_paths_test.cpp)
add_test(NAME model_paths_test COMMAND model_paths_test)
target_link_libraries(model_paths_test PRIVATE nesso_lib Catch2::Catch2WithMain)

add_executable(nesso_facade_test nesso_facade_test.cpp)
add_test(NAME nesso_facade_test COMMAND nesso_facade_test)
target_link_libraries(
  nesso_facade_test PRIVATE nesso_lib nesso_embed nesso_core
                            Catch2::Catch2WithMain)
target_compile_definitions(
  nesso_facade_test PRIVATE NESSO_TEST_FIXTURES="${NESSO_TEST_FIXTURES}"
                            ${NESSO_MODEL_TEST_DEFS})

add_executable(error_test error_test.cpp)
add_test(NAME error_test COMMAND error_test)
target_link_libraries(error_test PRIVATE nesso_lib nesso_parser nesso_embed
                                         nesso_core Catch2::Catch2WithMain)

add_executable(trace_test trace_test.cpp)
add_test(NAME trace_test COMMAND trace_test)
target_link_libraries(trace_test PRIVATE nesso_lib Catch2::Catch2WithMain)
# The harness only redirects stderr. trace.cpp stays in the coverage floor.
target_compile_options(trace_test PRIVATE -fno-profile-arcs -fno-test-coverage)

add_executable(nesso_flush_test nesso_flush_test.cpp)
add_test(NAME nesso_flush_test COMMAND nesso_flush_test)
target_link_libraries(nesso_flush_test PRIVATE nesso_lib nesso_embed nesso_core
                                               Catch2::Catch2WithMain)
# The harness throws and returns embed errors so nesso.cpp's flush edges run.
# gcc expands --coverage after -fno-profile-arcs, and _GLIBCXX_DEBUG then adds
# uncoverable branches that drop the branch floor. Strip those flags for this
# harness only. nesso.cpp stays in the floor.
set_property(
  TARGET nesso_flush_test
  PROPERTY CXX_COMPILER_LAUNCHER
           "${CMAKE_CURRENT_SOURCE_DIR}/strip-coverage-flags")

set_tests_properties(model_paths_test nesso_facade_test error_test trace_test
                     nesso_flush_test PROPERTIES LABELS unit)

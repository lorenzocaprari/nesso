find_package(Catch2 CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(onnxruntime CONFIG REQUIRED)

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
target_link_libraries(storage_engine_test PRIVATE mach_core
                                                  Catch2::Catch2WithMain)
target_include_directories(storage_engine_test
                           PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")

add_executable(distance_test distance_test.cpp)
add_test(NAME distance_test COMMAND distance_test)
target_link_libraries(distance_test PRIVATE mach_core Catch2::Catch2WithMain)
target_include_directories(distance_test
                           PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")

add_executable(vector_search_test vector_search_test.cpp)
add_test(NAME vector_search_test COMMAND vector_search_test)
target_link_libraries(vector_search_test PRIVATE mach_core
                                                 Catch2::Catch2WithMain)
target_include_directories(vector_search_test
                           PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")

add_executable(search_oracle_test search_oracle_test.cpp)
add_test(NAME search_oracle_test COMMAND search_oracle_test)
target_link_libraries(search_oracle_test PRIVATE mach_core
                                                 Catch2::Catch2WithMain)

add_executable(double_search_test double_search_test.cpp)
add_test(NAME double_search_test COMMAND double_search_test)
target_link_libraries(double_search_test PRIVATE mach_core)
# Exercise the double instantiations without adding test-harness branches to the
# coverage gate.
target_compile_options(double_search_test PRIVATE -fno-profile-arcs
                                                  -fno-test-coverage)

set(VECGREP_FIXTURES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

function(vecgrep_add_test name)
  add_executable(${name} ${name}.cpp)
  add_test(NAME ${name} COMMAND ${name})
  target_link_libraries(${name} PRIVATE mach_core Catch2::Catch2WithMain)
  target_compile_definitions(${name} PRIVATE FIXTURES_DIR="${VECGREP_FIXTURES_DIR}")
endfunction()

vecgrep_add_test(log_line_store_test)
vecgrep_add_test(vector_index_test)
vecgrep_add_test(embedder_test)
vecgrep_add_test(tokenizer_test)

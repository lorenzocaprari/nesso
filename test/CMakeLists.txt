find_package(Catch2 CONFIG REQUIRED)

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

add_executable(double_search_test double_search_test.cpp)
add_test(NAME double_search_test COMMAND double_search_test)
target_link_libraries(double_search_test PRIVATE mach_core)
# Exercise the double instantiations without adding test-harness branches to the
# coverage gate.
target_compile_options(double_search_test PRIVATE -fno-profile-arcs
                                                  -fno-test-coverage)

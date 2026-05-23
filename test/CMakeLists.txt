find_package(Catch2 CONFIG REQUIRED)

add_executable(storage_engine_test storage_engine_test.cpp)
add_test(NAME storage_engine_test COMMAND storage_engine_test)
target_link_libraries(storage_engine_test PRIVATE mach_core
                                                  Catch2::Catch2WithMain)
target_include_directories(storage_engine_test
                           PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")

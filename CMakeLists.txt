cmake_minimum_required(VERSION 4.2)

project(nesso LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

enable_testing()
add_subdirectory(src)
add_subdirectory(test)

option(NESSO_BUILD_BENCH "Build Google Benchmark microbenchmarks" OFF)
if(NESSO_BUILD_BENCH)
  add_subdirectory(bench)
endif()

install(TARGETS nesso RUNTIME DESTINATION bin)

cmake_minimum_required(VERSION 4.2)

project(nesso LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Set by scripts/build-release-binary and Dockerfile.runtime. Pipeline leaves it
# unset and keeps the profile's -flto=auto, which follows the CPU count. The
# longer prefix is first so the build directory is not rewritten twice.
if("$ENV{NESSO_REPRODUCIBLE}" MATCHES "^(1|ON|on|TRUE|true)$")
  add_compile_options(
    "-ffile-prefix-map=${CMAKE_BINARY_DIR}=/nesso/build"
    "-ffile-prefix-map=${CMAKE_SOURCE_DIR}=/nesso" "-frandom-seed=nesso"
    "-Werror=date-time")
  add_link_options("-frandom-seed=nesso" "-Wl,--build-id=sha1")
  if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options("-flto=1")
    add_link_options("-flto=1")
  endif()
endif()

enable_testing()
add_subdirectory(src)
add_subdirectory(test)
option(NESSO_BUILD_BENCH "Build Google Benchmark microbenchmarks" OFF)
if(NESSO_BUILD_BENCH)
  add_subdirectory(bench)
endif()

install(TARGETS nesso RUNTIME DESTINATION bin)

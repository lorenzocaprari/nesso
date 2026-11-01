cmake_minimum_required(VERSION 4.2)

project(nesso LANGUAGES CXX)

if(NESSO_VERSION)
  set(NESSO_BUILD_VERSION "${NESSO_VERSION}")
  set(NESSO_BUILD_REVISION "")
  set(_nesso_build_sha "$ENV{NESSO_BUILD_SHA}")
  set(_nesso_build_ref "$ENV{NESSO_BUILD_REF}")
  if(NOT _nesso_build_sha)
    set(_nesso_build_sha "$ENV{GITHUB_SHA}")
  endif()
  if(NOT _nesso_build_ref)
    set(_nesso_build_ref "$ENV{GITHUB_REF}")
  endif()
  string(LENGTH "${_nesso_build_sha}" _nesso_build_sha_length)
  if(_nesso_build_sha_length EQUAL 40 AND _nesso_build_sha MATCHES
                                          "^[0-9a-fA-F]+$")
    if(NOT _nesso_build_ref STREQUAL "refs/tags/v${NESSO_VERSION}")
      string(TOLOWER "${_nesso_build_sha}" _nesso_build_sha)
      string(SUBSTRING "${_nesso_build_sha}" 0 12 NESSO_BUILD_REVISION)
    endif()
  else()
    find_package(Git QUIET)
    if(GIT_FOUND)
      execute_process(
        COMMAND "${GIT_EXECUTABLE}" describe --exact-match --tags HEAD
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        OUTPUT_VARIABLE _nesso_git_tag
        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET
        RESULT_VARIABLE _nesso_git_tag_result)
      if(NOT _nesso_git_tag_result EQUAL 0 OR NOT _nesso_git_tag STREQUAL
                                              "v${NESSO_VERSION}")
        execute_process(
          COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
          WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
          OUTPUT_VARIABLE _nesso_build_sha
          OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET
          RESULT_VARIABLE _nesso_git_sha_result)
        string(LENGTH "${_nesso_build_sha}" _nesso_build_sha_length)
        if(_nesso_git_sha_result EQUAL 0
           AND _nesso_build_sha_length EQUAL 40
           AND _nesso_build_sha MATCHES "^[0-9a-fA-F]+$")
          string(TOLOWER "${_nesso_build_sha}" _nesso_build_sha)
          string(SUBSTRING "${_nesso_build_sha}" 0 12 NESSO_BUILD_REVISION)
        endif()
      endif()
    endif()
  endif()

  if(NESSO_BUILD_REVISION)
    string(APPEND NESSO_BUILD_VERSION "+g${NESSO_BUILD_REVISION}")
  endif()
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# CXX_EXTENSIONS does not propagate through an INTERFACE library, so a target
# that only picks up cxx_std_26 would still compile as gnu++26. The variables
# cover every target created in this project, including one that forgets the
# link. compiler.cppstd in the Conan profiles sets the same values.
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Off unless conanfile.py sees user.nesso:werror (CI passes -c; profiles do
# not).
option(NESSO_WERROR "Treat nesso compiler warnings as errors" OFF)

add_library(nesso_options INTERFACE)
target_compile_features(nesso_options INTERFACE cxx_std_26)
set_target_properties(nesso_options PROPERTIES CXX_EXTENSIONS OFF)

# -Werror is NESSO_WERROR, not a default. Clang-only -Wshorten-64-to-32 and
# -Wunreachable-code stay off: the first is not a shared GCC/Clang flag, and the
# second is a template heuristic.
set(NESSO_COMMON_WARNINGS
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wsign-conversion
    -Wshadow
    -Wformat=2
    -Wold-style-cast
    -Wnon-virtual-dtor
    -Woverloaded-virtual
    -Wnull-dereference
    -Wdouble-promotion
    -Wimplicit-fallthrough
    -Wcast-align
    -Wextra-semi)
set(NESSO_GCC_WARNINGS -Wduplicated-cond -Wduplicated-branches -Wlogical-op
                       -Wuseless-cast)

target_compile_options(
  nesso_options
  INTERFACE
    "$<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:${NESSO_COMMON_WARNINGS}>"
    "$<$<COMPILE_LANG_AND_ID:CXX,GNU>:${NESSO_GCC_WARNINGS}>"
    "$<$<AND:$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>,$<BOOL:${NESSO_WERROR}>>:-Werror>"
)

# Set by scripts/build-release-binary and packaging/Dockerfile.runtime. Pipeline
# leaves it unset and keeps the profile's -flto=auto, which follows the CPU
# count. The longer prefix is first so the build directory is not rewritten
# twice.
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

cmake_minimum_required(VERSION 4.2)

project(
  nesso
  VERSION 0.1.0
  LANGUAGES CXX)

if(NOT NESSO_VERSION)
  if(DEFINED ENV{NESSO_VERSION} AND NOT "$ENV{NESSO_VERSION}" STREQUAL "")
    set(NESSO_VERSION "$ENV{NESSO_VERSION}")
  else()
    set(NESSO_VERSION "${PROJECT_VERSION}")
  endif()
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

enable_testing()
add_subdirectory(src)
add_subdirectory(test)

install(TARGETS nesso RUNTIME DESTINATION bin)

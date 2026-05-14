cmake_minimum_required(VERSION 3.20)
project(mach1 LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_subdirectory(src)

install(TARGETS mach1 RUNTIME DESTINATION bin)

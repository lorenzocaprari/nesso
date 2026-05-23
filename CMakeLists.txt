cmake_minimum_required(VERSION 4.2)

project(mach1 LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

enable_testing()
add_subdirectory(src)
add_subdirectory(test)

install(TARGETS mach1 RUNTIME DESTINATION bin)

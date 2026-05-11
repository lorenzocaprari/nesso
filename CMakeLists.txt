cmake_minimum_required(VERSION 3.20)
project(mach1 LANGUAGES CXX)

add_subdirectory(src)

install(TARGETS mach1 RUNTIME DESTINATION bin)

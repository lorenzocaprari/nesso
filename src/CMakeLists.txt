find_package(CLI11 CONFIG REQUIRED)

add_subdirectory(mach_core)

add_executable(mach1 main.cpp)
target_link_libraries(mach1 PRIVATE mach_core CLI11::CLI11)

install(TARGETS mach1 RUNTIME DESTINATION bin)

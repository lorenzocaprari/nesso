find_package(CLI11 CONFIG REQUIRED)

add_subdirectory(core)
add_subdirectory(embed)
add_subdirectory(parser)

add_executable(nesso main.cpp grep_command.cpp storage_commands.cpp)
target_link_libraries(nesso PRIVATE core embed parser CLI11::CLI11)

install(TARGETS nesso RUNTIME DESTINATION bin)

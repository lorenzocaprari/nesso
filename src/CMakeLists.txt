find_package(CLI11 CONFIG REQUIRED)

add_subdirectory(core)
add_subdirectory(embed)
add_subdirectory(parser)
add_subdirectory(lib)

add_executable(nesso main.cpp cli/cli_parser.cpp cli/execute.cpp cli/output.cpp)
target_link_libraries(nesso PRIVATE nesso_lib CLI11::CLI11)
if(NESSO_VERSION)
  target_compile_definitions(nesso PRIVATE "NESSO_VERSION=\"${NESSO_VERSION}\"")
endif()

install(TARGETS nesso RUNTIME DESTINATION bin)

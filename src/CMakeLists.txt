add_subdirectory(platform)
add_subdirectory(core)
add_subdirectory(embed)
add_subdirectory(parser)
# Binary dir is not "nesso": the nesso executable must stay at build/<Config>/src/nesso.
add_subdirectory(nesso nesso_lib)
add_subdirectory(cli)

add_executable(nesso main.cpp)
target_link_libraries(nesso PRIVATE nesso_options nesso_lib nesso_cli)
if(NESSO_BUILD_VERSION)
  # cli_parser.cpp (in nesso_cli) embeds the version string for -V/--version.
  target_compile_definitions(nesso_cli
                             PRIVATE "NESSO_VERSION=\"${NESSO_BUILD_VERSION}\"")
endif()

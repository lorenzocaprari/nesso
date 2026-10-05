find_package(CLI11 CONFIG REQUIRED)

add_subdirectory(core)
add_subdirectory(embed)
add_subdirectory(parser)

add_library(nesso_lib nesso.cpp model_paths.cpp trace.cpp)
set_target_properties(nesso_lib PROPERTIES OUTPUT_NAME nesso)
target_include_directories(nesso_lib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(nesso_lib PRIVATE core embed parser)

add_executable(nesso main.cpp cli_parser.cpp cli.cpp)
target_link_libraries(nesso PRIVATE nesso_lib CLI11::CLI11)
if(NESSO_VERSION)
  target_compile_definitions(nesso PRIVATE "NESSO_VERSION=\"${NESSO_VERSION}\"")
endif()

install(TARGETS nesso RUNTIME DESTINATION bin)

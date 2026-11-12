find_package(CLI11 CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_subdirectory(platform)
add_subdirectory(core)
add_subdirectory(embed)
add_subdirectory(parser)

add_library(
  nesso_lib
  nesso.cpp
  nesso_errors.cpp
  nesso_scan.cpp
  nesso_grep.cpp
  nesso_index.cpp
  nesso_search.cpp
  model_paths.cpp
  trace.cpp
  render_json.cpp
  render_text.cpp)
set_target_properties(nesso_lib PROPERTIES OUTPUT_NAME nesso)
# nesso.hpp includes parser/archive_source.hpp. The parser include directory has
# to be public so the executable and the tests that include nesso.hpp can see
# it.
target_include_directories(
  nesso_lib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
                   "${CMAKE_CURRENT_SOURCE_DIR}/parser/include")
target_link_libraries(
  nesso_lib PRIVATE nesso_options nesso_core nesso_embed nesso_parser
                    nlohmann_json::nlohmann_json)

add_executable(nesso main.cpp cli_parser.cpp cli.cpp)
target_link_libraries(nesso PRIVATE nesso_options nesso_lib nesso_embed
                                    CLI11::CLI11 nlohmann_json::nlohmann_json)
if(NESSO_BUILD_VERSION)
  target_compile_definitions(nesso
                             PRIVATE "NESSO_VERSION=\"${NESSO_BUILD_VERSION}\"")
endif()

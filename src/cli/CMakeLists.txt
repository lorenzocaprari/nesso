find_package(CLI11 CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_library(nesso_cli cli.cpp cli_parser.cpp render_json.cpp render_text.cpp)
target_include_directories(
  nesso_cli
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(
  nesso_cli
  PUBLIC nesso_lib
  PRIVATE nesso_options CLI11::CLI11 nlohmann_json::nlohmann_json)

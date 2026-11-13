find_package(nlohmann_json CONFIG REQUIRED)

add_library(
  nesso_lib
  nesso.cpp
  nesso_errors.cpp
  nesso_scan.cpp
  nesso_grep.cpp
  nesso_index.cpp
  nesso_search.cpp
  model_paths.cpp
  trace.cpp)
set_target_properties(nesso_lib PROPERTIES OUTPUT_NAME nesso)
# nesso.hpp and error_map.hpp pull parser/core/embed types into the public
# surface, so those libraries stay PUBLIC for dependents that include them.
target_include_directories(
  nesso_lib
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(
  nesso_lib
  PUBLIC nesso_parser nesso_core nesso_embed
  PRIVATE nesso_options nlohmann_json::nlohmann_json)

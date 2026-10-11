add_library(nesso_parser log_chunker.cpp)
target_include_directories(nesso_parser
                           PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
find_package(nlohmann_json CONFIG REQUIRED)
target_link_libraries(nesso_parser PUBLIC nlohmann_json::nlohmann_json)

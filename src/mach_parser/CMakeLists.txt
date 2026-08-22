add_library(mach_parser log_chunker.cpp)
target_include_directories(mach_parser
                           PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
find_package(nlohmann_json CONFIG REQUIRED)
target_link_libraries(mach_parser PUBLIC nlohmann_json::nlohmann_json)

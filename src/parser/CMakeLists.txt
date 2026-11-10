add_library(nesso_parser log_chunker.cpp template_mask.cpp gzip_reader.cpp
                         input_walk.cpp archive_source.cpp)
target_include_directories(nesso_parser
                           PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
find_package(nlohmann_json CONFIG REQUIRED)
find_package(ZLIB REQUIRED)
find_package(LibArchive REQUIRED)
target_link_libraries(
  nesso_parser
  PUBLIC nlohmann_json::nlohmann_json
  PRIVATE nesso_options ZLIB::ZLIB LibArchive::LibArchive)

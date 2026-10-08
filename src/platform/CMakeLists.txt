add_library(platform file.cpp)
target_include_directories(platform
                           PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

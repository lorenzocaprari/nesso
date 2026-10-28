add_library(nesso_platform file.cpp)
target_include_directories(nesso_platform
                           PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(nesso_platform PRIVATE nesso_options)

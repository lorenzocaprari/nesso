add_library(nesso_lib nesso.cpp model_paths.cpp)
set_target_properties(nesso_lib PROPERTIES OUTPUT_NAME nesso)
target_include_directories(nesso_lib
                           PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(nesso_lib PRIVATE core embed parser)

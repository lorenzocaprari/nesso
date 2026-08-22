add_library(mach_embed wordpiece_tokenizer.cpp)
target_include_directories(mach_embed
                           PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

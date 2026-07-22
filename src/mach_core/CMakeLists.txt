add_library(mach_core storage_engine.cpp distance.cpp vector_search.cpp)
target_include_directories(mach_core
                           PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

add_library(mach_core storage_engine.cpp distance.cpp vector_search.cpp)
target_include_directories(mach_core
                           PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
  target_compile_options(mach_core PRIVATE -mavx2)
endif()

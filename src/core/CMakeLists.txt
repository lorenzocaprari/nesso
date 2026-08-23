add_library(core storage_engine.cpp distance.cpp vector_search.cpp
                 embedding_store.cpp)
target_include_directories(core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
  target_compile_options(core PRIVATE -mavx2)
endif()

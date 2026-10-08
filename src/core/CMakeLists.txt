add_library(core distance.cpp corpus_index.cpp)
target_include_directories(core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(core PUBLIC platform)
# AVX2 stays on this translation unit. Applying it to the whole library lets LTO
# copy those instructions into generic callers and SIGILL on older CPUs.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64" AND NOT MSVC)
  target_sources(core PRIVATE distance_avx2.cpp)
  set_source_files_properties(distance_avx2.cpp PROPERTIES COMPILE_OPTIONS
                                                           "-mavx2;-mfma")
endif()

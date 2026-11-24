add_library(
  nesso_core
  distance.cpp
  corpus_format.cpp
  corpus_policy.cpp
  corpus_lexical.cpp
  corpus_writer.cpp
  corpus_reader.cpp
  dedup.cpp
  embed_order.cpp
  event_types.cpp
  bm25.cpp
  lexical_terms.cpp
  query_class.cpp
  score_fusion.cpp
  sha256.cpp
  top_k.cpp)
find_package(Threads REQUIRED)
target_include_directories(
  nesso_core
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(
  nesso_core
  PUBLIC nesso_platform Threads::Threads
  PRIVATE nesso_options nesso_parser)
# Baseline ISA for this translation unit. The kernels use target("avx2,fma"),
# which keeps those instructions out of generic callers under LTO. A file-wide
# -mavx2/-mfma also compiles inline functions this file emits (libstdc++ debug
# helpers reached from std::span::operator[] at -O0) with AVX2 encodings. The
# linker can keep that COMDAT for the whole program and SIGILL on a CPU without
# AVX2. clang-cl reports MSVC=TRUE but still compiles the GNU target attributes
# in this TU. Pure MSVC cl.exe stays on the scalar path until a /arch:AVX2 port.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
  if(NOT MSVC OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_sources(nesso_core PRIVATE distance_avx2.cpp)
  endif()
endif()

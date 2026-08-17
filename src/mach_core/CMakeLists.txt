add_library(
  mach_core
  storage_engine.cpp
  distance.cpp
  vector_search.cpp
  log_line_store.cpp
  vector_index.cpp)
target_include_directories(mach_core
                           PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(
  mach_core PUBLIC nlohmann_json::nlohmann_json onnxruntime::onnxruntime
                   Threads::Threads)

add_library(nesso_tokenizer wordpiece_tokenizer.cpp)
target_include_directories(nesso_tokenizer
                           PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(nesso_tokenizer PRIVATE nesso_options)

add_library(nesso_embed amx_guard.cpp batch_plan.cpp onnx_embedder.cpp
                        session_policy.cpp)
target_include_directories(nesso_embed
                           PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
find_package(onnxruntime CONFIG REQUIRED)
target_link_libraries(
  nesso_embed
  PUBLIC nesso_tokenizer onnxruntime::onnxruntime
  PRIVATE nesso_options)

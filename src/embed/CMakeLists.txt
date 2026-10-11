add_library(nesso_tokenizer wordpiece_tokenizer.cpp)
target_include_directories(nesso_tokenizer
                           PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

add_library(nesso_embed onnx_embedder.cpp)
target_include_directories(nesso_embed
                           PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
find_package(onnxruntime CONFIG REQUIRED)
target_link_libraries(nesso_embed PUBLIC nesso_tokenizer
                                         onnxruntime::onnxruntime)

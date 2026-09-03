add_library(tokenizer wordpiece_tokenizer.cpp)
target_include_directories(tokenizer
                           PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

add_library(embed onnx_embedder.cpp)
target_include_directories(embed PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
find_package(onnxruntime CONFIG REQUIRED)
target_link_libraries(embed PUBLIC tokenizer onnxruntime::onnxruntime)

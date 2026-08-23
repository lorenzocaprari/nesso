add_library(embed wordpiece_tokenizer.cpp onnx_embedder.cpp)
target_include_directories(embed PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
find_package(onnxruntime CONFIG REQUIRED)
target_link_libraries(embed PUBLIC onnxruntime::onnxruntime)

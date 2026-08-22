add_library(mach_embed wordpiece_tokenizer.cpp onnx_embedder.cpp)
target_include_directories(mach_embed
                           PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
find_package(onnxruntime CONFIG REQUIRED)
target_link_libraries(mach_embed PUBLIC onnxruntime::onnxruntime)

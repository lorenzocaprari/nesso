add_executable(onnx_embedder_test onnx_embedder_test.cpp)
target_link_libraries(onnx_embedder_test PRIVATE embed Catch2::Catch2WithMain)
target_compile_definitions(onnx_embedder_test PRIVATE ${NESSO_MODEL_TEST_DEFS})
# Skips when models/ is absent. An instrumented skip would record no hits and
# fail the floor.
target_compile_options(onnx_embedder_test PRIVATE -fno-profile-arcs
                                                  -fno-test-coverage)
add_test(NAME onnx_embedder_test COMMAND $<TARGET_FILE:onnx_embedder_test>
                                         ${NESSO_MODEL_TEST_ARGS})
set_tests_properties(onnx_embedder_test PROPERTIES LABELS unit)

add_executable(batch_plan_test batch_plan_test.cpp)
target_link_libraries(batch_plan_test PRIVATE nesso_embed
                                              Catch2::Catch2WithMain)
# Catch2 branches in this harness sit under the tree-wide branch floor.
# batch_plan.cpp stays instrumented.
target_compile_options(batch_plan_test PRIVATE -fno-profile-arcs
                                               -fno-test-coverage)
add_test(NAME batch_plan_test COMMAND batch_plan_test)

add_executable(onnx_embedder_test onnx_embedder_test.cpp)
target_link_libraries(onnx_embedder_test PRIVATE nesso_embed
                                                 Catch2::Catch2WithMain)
target_compile_definitions(onnx_embedder_test PRIVATE ${NESSO_MODEL_TEST_DEFS})
# Skips when models/ is absent. An instrumented skip would record no hits and
# fail the floor.
target_compile_options(onnx_embedder_test PRIVATE -fno-profile-arcs
                                                  -fno-test-coverage)
add_test(NAME onnx_embedder_test COMMAND $<TARGET_FILE:onnx_embedder_test>
                                         ${NESSO_MODEL_TEST_ARGS})

add_executable(session_pool_test session_pool_test.cpp)
target_link_libraries(session_pool_test PRIVATE nesso_embed
                                                Catch2::Catch2WithMain)
target_compile_definitions(session_pool_test PRIVATE ${NESSO_MODEL_TEST_DEFS})
target_compile_options(session_pool_test PRIVATE -fno-profile-arcs
                                                 -fno-test-coverage)
add_test(NAME session_pool_test COMMAND $<TARGET_FILE:session_pool_test>
                                        ${NESSO_MODEL_TEST_ARGS})
add_executable(session_policy_test session_policy_test.cpp)
target_link_libraries(session_policy_test PRIVATE nesso_embed
                                                  Catch2::Catch2WithMain)
add_test(NAME session_policy_test COMMAND session_policy_test)
add_executable(amx_guard_test amx_guard_test.cpp)
target_link_libraries(amx_guard_test PRIVATE nesso_embed Catch2::Catch2WithMain)
add_test(NAME amx_guard_test COMMAND amx_guard_test)
set_tests_properties(batch_plan_test onnx_embedder_test session_pool_test
                     session_policy_test amx_guard_test PROPERTIES LABELS unit)

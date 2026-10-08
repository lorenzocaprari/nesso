add_executable(wordpiece_tokenizer_test wordpiece_tokenizer_test.cpp)
add_test(NAME wordpiece_tokenizer_test COMMAND wordpiece_tokenizer_test)
target_link_libraries(wordpiece_tokenizer_test PRIVATE tokenizer
                                                       Catch2::Catch2WithMain)
target_compile_definitions(
  wordpiece_tokenizer_test PRIVATE NESSO_TEST_FIXTURES="${NESSO_TEST_FIXTURES}"
                                   ${NESSO_MODEL_TEST_DEFS})
set_tests_properties(wordpiece_tokenizer_test PROPERTIES LABELS unit)

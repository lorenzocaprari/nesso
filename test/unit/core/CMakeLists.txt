add_executable(distance_test distance_test.cpp)
add_test(NAME distance_test COMMAND distance_test)
target_link_libraries(distance_test PRIVATE nesso_core Catch2::Catch2WithMain)

add_executable(corpus_index_test corpus_index_test.cpp)
add_test(NAME corpus_index_test COMMAND corpus_index_test)
target_link_libraries(corpus_index_test PRIVATE nesso_core
                                                Catch2::Catch2WithMain)
target_compile_definitions(
  corpus_index_test
  PRIVATE
    "NESSO_ONE_CHUNK_FIXTURE=\"${CMAKE_SOURCE_DIR}/test/functional/features/testdata/one-chunk.nesso\""
    "NESSO_MISMATCH_FIXTURE=\"${CMAKE_SOURCE_DIR}/test/functional/features/testdata/mismatch.nesso\""
    "NESSO_OTHER_MODEL_FIXTURE=\"${CMAKE_SOURCE_DIR}/test/functional/features/testdata/other-model.nesso\""
    "NESSO_FUTURE_VERSION_FIXTURE=\"${CMAKE_SOURCE_DIR}/test/functional/features/testdata/future-version.nesso\""
)

add_executable(top_k_test top_k_test.cpp)
add_test(NAME top_k_test COMMAND top_k_test)
target_link_libraries(top_k_test PRIVATE nesso_core Catch2::Catch2WithMain)

add_executable(search_oracle_test search_oracle_test.cpp)
add_test(NAME search_oracle_test COMMAND search_oracle_test)
target_link_libraries(search_oracle_test PRIVATE nesso_core
                                                 Catch2::Catch2WithMain)

set_tests_properties(distance_test corpus_index_test top_k_test
                     search_oracle_test PROPERTIES LABELS unit)

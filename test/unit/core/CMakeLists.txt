add_executable(distance_test distance_test.cpp)
add_test(NAME distance_test COMMAND distance_test)
target_link_libraries(distance_test PRIVATE nesso_options nesso_core
                                            Catch2::Catch2WithMain)

add_executable(corpus_index_test corpus_index_test.cpp)
add_test(NAME corpus_index_test COMMAND corpus_index_test)
target_link_libraries(corpus_index_test PRIVATE nesso_options nesso_core
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
target_link_libraries(top_k_test PRIVATE nesso_options nesso_core
                                         Catch2::Catch2WithMain)

add_executable(search_oracle_test search_oracle_test.cpp)
add_test(NAME search_oracle_test COMMAND search_oracle_test)
target_link_libraries(search_oracle_test PRIVATE nesso_options nesso_core
                                                 Catch2::Catch2WithMain)

add_executable(dedup_test dedup_test.cpp)
add_test(NAME dedup_test COMMAND dedup_test)
target_include_directories(dedup_test
                           PRIVATE "${CMAKE_SOURCE_DIR}/src/embed/include")
target_link_libraries(dedup_test PRIVATE nesso_core Catch2::Catch2WithMain)

add_executable(sha256_test sha256_test.cpp)
add_test(NAME sha256_test COMMAND sha256_test)
target_link_libraries(sha256_test PRIVATE nesso_options nesso_core
                                          Catch2::Catch2WithMain)

add_executable(lexical_terms_test lexical_terms_test.cpp)
add_test(NAME lexical_terms_test COMMAND lexical_terms_test)
target_link_libraries(lexical_terms_test PRIVATE nesso_options nesso_core
                                                 Catch2::Catch2WithMain)

add_executable(bm25_test bm25_test.cpp)
add_test(NAME bm25_test COMMAND bm25_test)
target_link_libraries(bm25_test PRIVATE nesso_options nesso_core
                                        Catch2::Catch2WithMain)
# Catch2 checks add uncoverable branches. The library this calls stays in the
# floor.
set_property(
  TARGET dedup_test
  PROPERTY CXX_COMPILER_LAUNCHER
           "${CMAKE_SOURCE_DIR}/test/unit/nesso/strip-coverage-flags")

set_tests_properties(
  distance_test
  corpus_index_test
  top_k_test
  search_oracle_test
  dedup_test
  sha256_test
  lexical_terms_test
  bm25_test
  PROPERTIES LABELS unit)

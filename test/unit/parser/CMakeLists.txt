add_executable(log_chunker_test log_chunker_test.cpp corpus_records_test.cpp)
add_executable(template_mask_test template_mask_test.cpp)
add_executable(input_sources_test input_sources_test.cpp)
add_executable(archive_source_test archive_source_test.cpp)
find_package(LibArchive REQUIRED)
add_test(NAME log_chunker_test COMMAND log_chunker_test)
target_link_libraries(log_chunker_test PRIVATE nesso_options nesso_parser
                                               Catch2::Catch2WithMain)
target_compile_definitions(log_chunker_test
                           PRIVATE NESSO_TEST_FIXTURES="${NESSO_TEST_FIXTURES}")
# Harness stays out of the coverage floor. LogChunker is still measured through
# the instrumented nesso_parser library.
target_compile_options(log_chunker_test PRIVATE -fno-profile-arcs
                                                -fno-test-coverage)
add_test(NAME template_mask_test COMMAND template_mask_test)
target_link_libraries(template_mask_test PRIVATE nesso_options nesso_parser
                                                 Catch2::Catch2WithMain)
# Harness stays out of the coverage floor. template_mask.cpp is still measured
# through the instrumented nesso_parser library.
target_compile_options(template_mask_test PRIVATE -fno-profile-arcs
                                                  -fno-test-coverage)
add_test(NAME input_sources_test COMMAND input_sources_test)
target_link_libraries(input_sources_test PRIVATE nesso_options nesso_parser
                                                 Catch2::Catch2WithMain)
add_test(NAME archive_source_test COMMAND archive_source_test)
target_link_libraries(
  archive_source_test PRIVATE nesso_options nesso_parser LibArchive::LibArchive
                              Catch2::Catch2WithMain)
# Harness stays out of the coverage floor. The parser library is still measured.
target_compile_options(input_sources_test PRIVATE -fno-profile-arcs
                                                  -fno-test-coverage)
target_compile_options(archive_source_test PRIVATE -fno-profile-arcs
                                                   -fno-test-coverage)
set_tests_properties(log_chunker_test template_mask_test input_sources_test
                     archive_source_test PROPERTIES LABELS unit)

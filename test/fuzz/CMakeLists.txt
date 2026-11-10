add_executable(nesso_fuzz_parser fuzz_parser.cpp)
target_link_libraries(nesso_fuzz_parser PRIVATE nesso_options nesso_parser)

add_executable(nesso_fuzz_tokenizer fuzz_tokenizer.cpp)
target_link_libraries(nesso_fuzz_tokenizer PRIVATE nesso_options
                                                   nesso_tokenizer)

add_executable(nesso_fuzz_archive_source fuzz_archive_source.cpp)
target_link_libraries(nesso_fuzz_archive_source PRIVATE nesso_options
                                                        nesso_parser)

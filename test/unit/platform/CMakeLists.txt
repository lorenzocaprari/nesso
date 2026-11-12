add_executable(file_test file_test.cpp)
add_test(NAME file_test COMMAND file_test)
target_link_libraries(file_test PRIVATE nesso_options nesso_platform
                                        Catch2::Catch2WithMain)

add_executable(narrow_cast_test narrow_cast_test.cpp)
add_test(NAME narrow_cast_test COMMAND narrow_cast_test)
target_link_libraries(narrow_cast_test PRIVATE nesso_options nesso_platform
                                               Catch2::Catch2WithMain)

set_tests_properties(file_test narrow_cast_test PROPERTIES LABELS unit)

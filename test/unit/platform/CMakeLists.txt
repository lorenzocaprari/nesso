add_executable(file_test file_test.cpp)
add_test(NAME file_test COMMAND file_test)
target_link_libraries(file_test PRIVATE platform Catch2::Catch2WithMain)
set_tests_properties(file_test PROPERTIES LABELS unit)

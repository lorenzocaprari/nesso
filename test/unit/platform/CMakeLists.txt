add_executable(file_test file_test.cpp)
add_test(NAME file_test COMMAND file_test)
target_link_libraries(file_test PRIVATE nesso_options nesso_platform
                                        Catch2::Catch2WithMain)

add_executable(narrow_cast_test narrow_cast_test.cpp)
add_test(NAME narrow_cast_test COMMAND narrow_cast_test)
target_link_libraries(narrow_cast_test PRIVATE nesso_options nesso_platform
                                               Catch2::Catch2WithMain)

# Toolchain probe only. nesso_options supplies the warning set and C++26. No
# nesso library is linked, so the probe stays out of the product binary.
add_executable(experimental_simd_probe_test experimental_simd_probe_test.cpp)
add_test(NAME experimental_simd_probe_test COMMAND experimental_simd_probe_test)
target_link_libraries(experimental_simd_probe_test
                      PRIVATE nesso_options Catch2::Catch2WithMain)

add_executable(assert_test assert_test.cpp)
add_test(NAME assert_test COMMAND assert_test)
target_link_libraries(assert_test PRIVATE nesso_options nesso_platform
                                          Catch2::Catch2WithMain)

set_tests_properties(file_test narrow_cast_test experimental_simd_probe_test
                     assert_test PROPERTIES LABELS unit)

find_program(NESSO_PYTHON_EXECUTABLE python3)
if(NESSO_PYTHON_EXECUTABLE)
  add_test(NAME check_determinism_test
           COMMAND "${NESSO_PYTHON_EXECUTABLE}"
                   "${CMAKE_CURRENT_SOURCE_DIR}/check_determinism_test.py")
  set_tests_properties(check_determinism_test PROPERTIES LABELS unit)
endif()

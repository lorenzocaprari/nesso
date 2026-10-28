find_program(NESSO_PYTHON_EXECUTABLE python3)
if(NESSO_PYTHON_EXECUTABLE)
  add_test(NAME check_determinism_test
           COMMAND "${NESSO_PYTHON_EXECUTABLE}"
                   "${CMAKE_CURRENT_SOURCE_DIR}/check_determinism_test.py")
  set_tests_properties(check_determinism_test PROPERTIES LABELS unit)
  add_test(NAME check_no_runpath_test
           COMMAND "${NESSO_PYTHON_EXECUTABLE}"
                   "${CMAKE_CURRENT_SOURCE_DIR}/check_no_runpath_test.py")
  set_tests_properties(check_no_runpath_test PROPERTIES LABELS unit)
  add_test(NAME check_network_free_test
           COMMAND "${NESSO_PYTHON_EXECUTABLE}"
                   "${CMAKE_CURRENT_SOURCE_DIR}/check_network_free_test.py")
  set_tests_properties(check_network_free_test PROPERTIES LABELS unit)
  add_test(NAME package_deb_test
           COMMAND "${NESSO_PYTHON_EXECUTABLE}"
                   "${CMAKE_CURRENT_SOURCE_DIR}/package_deb_test.py")
  set_tests_properties(package_deb_test PROPERTIES LABELS unit)
  add_test(NAME check_agent_docs_test
           COMMAND "${NESSO_PYTHON_EXECUTABLE}"
                   "${CMAKE_CURRENT_SOURCE_DIR}/check_agent_docs_test.py")
  set_tests_properties(check_agent_docs_test PROPERTIES LABELS unit)
  add_test(NAME bench_compare_test
           COMMAND "${NESSO_PYTHON_EXECUTABLE}"
                   "${CMAKE_CURRENT_SOURCE_DIR}/bench_compare_test.py")
  set_tests_properties(bench_compare_test PROPERTIES LABELS unit)
  add_test(NAME bench_hdfs_test
           COMMAND "${NESSO_PYTHON_EXECUTABLE}"
                   "${CMAKE_CURRENT_SOURCE_DIR}/bench_hdfs_test.py")
  set_tests_properties(bench_hdfs_test PROPERTIES LABELS unit)
  add_test(NAME bench_logdir_test
           COMMAND "${NESSO_PYTHON_EXECUTABLE}"
                   "${CMAKE_CURRENT_SOURCE_DIR}/bench_logdir_test.py")
  set_tests_properties(bench_logdir_test PROPERTIES LABELS unit)
  add_test(NAME fetch_loghub_test
           COMMAND "${NESSO_PYTHON_EXECUTABLE}"
                   "${CMAKE_CURRENT_SOURCE_DIR}/fetch_loghub_test.py")
  set_tests_properties(fetch_loghub_test PROPERTIES LABELS unit)
  add_test(NAME ci_build_archive_test
           COMMAND "${NESSO_PYTHON_EXECUTABLE}"
                   "${CMAKE_CURRENT_SOURCE_DIR}/ci_build_archive_test.py")
  set_tests_properties(ci_build_archive_test PROPERTIES LABELS unit)
  add_test(NAME third_party_notices_test
           COMMAND "${NESSO_PYTHON_EXECUTABLE}"
                   "${CMAKE_CURRENT_SOURCE_DIR}/third_party_notices_test.py")
  set_tests_properties(third_party_notices_test PROPERTIES LABELS unit)
  add_test(NAME check_runtime_licenses_test
           COMMAND "${NESSO_PYTHON_EXECUTABLE}"
                   "${CMAKE_CURRENT_SOURCE_DIR}/check_runtime_licenses_test.py")
  set_tests_properties(check_runtime_licenses_test PROPERTIES LABELS unit)
  add_test(NAME build_env_hash_test
           COMMAND "${NESSO_PYTHON_EXECUTABLE}"
                   "${CMAKE_CURRENT_SOURCE_DIR}/build_env_hash_test.py")
  set_tests_properties(build_env_hash_test PROPERTIES LABELS unit)
  add_test(NAME demo_log_test
           COMMAND "${NESSO_PYTHON_EXECUTABLE}"
                   "${CMAKE_CURRENT_SOURCE_DIR}/demo_log_test.py")
  set_tests_properties(demo_log_test PROPERTIES LABELS unit)
endif()

find_program(NESSO_GO_EXECUTABLE go)
if(NESSO_GO_EXECUTABLE)
  add_test(
    NAME deb_repro_test
    COMMAND "${NESSO_GO_EXECUTABLE}" test -count=1 .
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
  set_tests_properties(deb_repro_test PROPERTIES LABELS unit)
  add_test(
    NAME ci_report_test
    COMMAND "${NESSO_GO_EXECUTABLE}" test -count=1 .
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/ci")
  set_tests_properties(ci_report_test PROPERTIES LABELS unit)
endif()

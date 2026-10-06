# NESSO_REQUIRE_BDD=1 turns a missing Go toolchain into a configure error
# instead of silently dropping the black-box specs.
if(NOT DEFINED NESSO_REQUIRE_BDD)
  if("$ENV{NESSO_REQUIRE_BDD}" MATCHES "^(1|ON|on|TRUE|true)$")
    set(NESSO_REQUIRE_BDD ON)
  else()
    set(NESSO_REQUIRE_BDD OFF)
  endif()
endif()

find_program(NESSO_GO_EXECUTABLE go)
if(NOT NESSO_GO_EXECUTABLE)
  if(NESSO_REQUIRE_BDD)
    message(
      FATAL_ERROR "NESSO_REQUIRE_BDD is set but no go toolchain was found")
  endif()
  message(STATUS "go not found: bdd test disabled")
  return()
endif()

add_test(
  NAME bdd
  COMMAND "${NESSO_GO_EXECUTABLE}" test -count=1 ./...
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
set_tests_properties(
  bdd
  PROPERTIES
    ENVIRONMENT
    "NESSO_BINARY=$<TARGET_FILE:nesso>;NESSO_MODEL_DIR=${CMAKE_SOURCE_DIR}/models"
)

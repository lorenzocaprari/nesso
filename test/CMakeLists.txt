find_package(Catch2 CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(onnxruntime CONFIG REQUIRED)

file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
     DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")
set(NESSO_TEST_FIXTURES "${CMAKE_CURRENT_BINARY_DIR}/fixtures")
set(NESSO_MODELS_DIR "${CMAKE_SOURCE_DIR}/models")

# NESSO_REQUIRE_MODEL=1 at configure time fails model-backed tests instead of
# skipping them, and drops Catch2's --allow-running-no-tests escape hatch.
if(NOT DEFINED NESSO_REQUIRE_MODEL)
  if("$ENV{NESSO_REQUIRE_MODEL}" MATCHES "^(1|ON|on|TRUE|true)$")
    set(NESSO_REQUIRE_MODEL ON)
  else()
    set(NESSO_REQUIRE_MODEL OFF)
  endif()
endif()
set(NESSO_MODEL_TEST_DEFS "NESSO_MODELS_DIR=\"${NESSO_MODELS_DIR}\"")
set(NESSO_MODEL_TEST_ARGS)
if(NESSO_REQUIRE_MODEL)
  list(APPEND NESSO_MODEL_TEST_DEFS NESSO_REQUIRE_MODEL=1)
else()
  set(NESSO_MODEL_TEST_ARGS --allow-running-no-tests)
endif()

add_subdirectory(unit)
add_subdirectory(integration)
add_subdirectory(fuzz)
add_subdirectory(functional)

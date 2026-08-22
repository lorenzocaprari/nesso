find_package(Catch2 CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(onnxruntime CONFIG REQUIRED)

function(add_catch_test name)
  cmake_parse_arguments(ARG "" "" "SOURCES;LIBS" ${ARGN})
  if(NOT ARG_SOURCES)
    set(ARG_SOURCES "${name}.cpp")
  endif()

  add_executable(${name} ${ARG_SOURCES})
  add_test(NAME ${name} COMMAND ${name})
  target_link_libraries(${name} PRIVATE Catch2::Catch2WithMain ${ARG_LIBS})
endfunction()

add_catch_test(dependency_link_test LIBS nlohmann_json::nlohmann_json
               onnxruntime::onnxruntime)
target_compile_options(dependency_link_test PRIVATE -fno-profile-arcs
                                                    -fno-test-coverage)

add_catch_test(storage_engine_test LIBS mach_core)
add_catch_test(distance_test LIBS mach_core)
add_catch_test(vector_search_test LIBS mach_core)
add_catch_test(search_oracle_test LIBS mach_core)

add_executable(double_search_test double_search_test.cpp)
add_test(NAME double_search_test COMMAND double_search_test)
target_link_libraries(double_search_test PRIVATE mach_core)
target_compile_options(double_search_test PRIVATE -fno-profile-arcs
                                                  -fno-test-coverage)

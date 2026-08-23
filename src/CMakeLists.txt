find_package(CLI11 CONFIG REQUIRED)

add_subdirectory(core)
add_subdirectory(embed)
add_subdirectory(parser)

set(NESSO_STORAGE_SRCS main.cpp storage_commands.cpp)

add_executable(nesso ${NESSO_STORAGE_SRCS})
target_link_libraries(nesso PRIVATE core CLI11::CLI11)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/grep_command.cpp")
  target_sources(nesso PRIVATE grep_command.cpp)
  target_link_libraries(nesso PRIVATE embed parser)

  # AFL fuzz exercises storage search only; skip ONNX/grep (clang+AFL vs gcc
  # static ORT).
  add_executable(nesso_fuzz ${NESSO_STORAGE_SRCS})
  target_compile_definitions(nesso_fuzz PRIVATE NESSO_FUZZ_STORAGE_ONLY)
  target_link_libraries(nesso_fuzz PRIVATE core CLI11::CLI11)
endif()

install(TARGETS nesso RUNTIME DESTINATION bin)

find_package(CLI11 CONFIG REQUIRED)

add_subdirectory(mach_core)
add_subdirectory(mach_embed)
add_subdirectory(mach_parser)

set(MACH1_STORAGE_SRCS main.cpp commands/storage_commands.cpp)

add_executable(mach1 ${MACH1_STORAGE_SRCS})
target_link_libraries(mach1 PRIVATE mach_core CLI11::CLI11)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/commands/grep_command.cpp")
  target_sources(mach1 PRIVATE commands/grep_command.cpp)
  target_link_libraries(mach1 PRIVATE mach_embed mach_parser)

  # AFL fuzz exercises storage search only; skip ONNX/grep (clang+AFL vs gcc
  # static ORT).
  add_executable(mach1_fuzz ${MACH1_STORAGE_SRCS})
  target_compile_definitions(mach1_fuzz PRIVATE MACH1_FUZZ_STORAGE_ONLY)
  target_link_libraries(mach1_fuzz PRIVATE mach_core CLI11::CLI11)
endif()

install(TARGETS mach1 RUNTIME DESTINATION bin)

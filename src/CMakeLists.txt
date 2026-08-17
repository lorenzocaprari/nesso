find_package(CLI11 CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(onnxruntime CONFIG REQUIRED)
find_package(Threads REQUIRED)

add_subdirectory(mach_core)

add_executable(vecgrep main.cpp)
target_link_libraries(vecgrep PRIVATE mach_core CLI11::CLI11)

install(TARGETS vecgrep RUNTIME DESTINATION bin)

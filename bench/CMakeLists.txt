find_package(benchmark CONFIG REQUIRED)

add_executable(nesso_bench nesso_bench.cpp)
target_link_libraries(nesso_bench PRIVATE core tokenizer benchmark::benchmark)
target_compile_definitions(
  nesso_bench
  PRIVATE NESSO_BENCH_VOCAB="${CMAKE_SOURCE_DIR}/test/fixtures/tiny_vocab.txt")

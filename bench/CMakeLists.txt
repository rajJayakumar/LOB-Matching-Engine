include(FetchContent)

FetchContent_Declare(
  googlebenchmark
  GIT_REPOSITORY https://github.com/google/benchmark.git
  GIT_TAG        v1.9.1
)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googlebenchmark)

# --- Micro-benchmarks ---------------------------------------------------
add_executable(bench_micro bench_micro.cpp)
target_link_libraries(bench_micro PRIVATE ob benchmark::benchmark benchmark::benchmark_main)
target_compile_options(bench_micro PRIVATE -O3 -fno-omit-frame-pointer)
if(OB_NATIVE_ARCH)
    target_compile_options(bench_micro PRIVATE -march=native)
endif()

# --- Full-session replay benchmark (own main, no Google Benchmark) ------
add_executable(bench_replay bench_replay.cpp)
target_link_libraries(bench_replay PRIVATE ob)
target_compile_options(bench_replay PRIVATE -O3 -fno-omit-frame-pointer)
if(OB_NATIVE_ARCH)
    target_compile_options(bench_replay PRIVATE -march=native)
endif()

# These targets are NOT registered with ctest — they are invoked explicitly.

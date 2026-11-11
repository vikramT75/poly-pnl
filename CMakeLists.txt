cmake_minimum_required(VERSION 3.20)
project(gravia_pnl VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------------------------------------------------------------------
# Build options
# ---------------------------------------------------------------------------
option(GRAVIA_ENABLE_ASAN    "Enable AddressSanitizer"           OFF)
option(GRAVIA_ENABLE_UBSAN   "Enable UndefinedBehaviorSanitizer" OFF)
option(GRAVIA_ENABLE_TSAN    "Enable ThreadSanitizer"            OFF)
option(GRAVIA_ENABLE_BENCH   "Build benchmarks"                  OFF)
option(GRAVIA_ENABLE_FUZZ    "Build fuzz targets"                OFF)
option(GRAVIA_NATIVE_MARCH   "Use -march=native (for benchmarks)" OFF)

# ---------------------------------------------------------------------------
# Compiler flags
# ---------------------------------------------------------------------------
add_library(gravia_compile_options INTERFACE)

target_compile_options(gravia_compile_options INTERFACE
    -Wall -Wextra
    -Wno-unused-parameter
    -Wno-pedantic
    -fno-exceptions
    -fno-rtti
    $<$<CONFIG:Release>:-O3>
    $<$<CONFIG:RelWithDebInfo>:-O2 -g>
    $<$<CONFIG:Debug>:-O0 -g>
)

if(GRAVIA_NATIVE_MARCH)
    target_compile_options(gravia_compile_options INTERFACE -march=native)
endif()

if(GRAVIA_ENABLE_ASAN)
    target_compile_options(gravia_compile_options INTERFACE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(gravia_compile_options INTERFACE -fsanitize=address)
endif()

if(GRAVIA_ENABLE_UBSAN)
    target_compile_options(gravia_compile_options INTERFACE -fsanitize=undefined)
    target_link_options(gravia_compile_options INTERFACE -fsanitize=undefined)
endif()

if(GRAVIA_ENABLE_TSAN)
    target_compile_options(gravia_compile_options INTERFACE -fsanitize=thread)
    target_link_options(gravia_compile_options INTERFACE -fsanitize=thread)
endif()

# ---------------------------------------------------------------------------
# Core library — decoder + engine
# ---------------------------------------------------------------------------
add_library(gravia_core STATIC
    core/src/decoder/decoder.cpp
    core/src/engine/engine.cpp
)

target_include_directories(gravia_core PUBLIC
    ${CMAKE_SOURCE_DIR}/core/include
)

target_link_libraries(gravia_core PUBLIC gravia_compile_options)

# ---------------------------------------------------------------------------
# IPC daemon — reads from Go ingestor socket, feeds engine
# ---------------------------------------------------------------------------
add_executable(gravia_engine_daemon
    core/src/daemon/main.cpp
    core/src/daemon/socket_reader.cpp
    core/src/daemon/snapshot.cpp
    core/src/daemon/query_server.cpp
)

target_link_libraries(gravia_engine_daemon PRIVATE gravia_core pthread)

# ---------------------------------------------------------------------------
# Tests (via GoogleTest)
# ---------------------------------------------------------------------------
enable_testing()

find_package(GTest QUIET)
if(GTest_FOUND)
    add_executable(gravia_tests
        tests/unit/decoder_test.cpp
        tests/unit/engine_test.cpp
        tests/unit/fixedpoint_test.cpp
        tests/property/determinism_test.cpp
        tests/property/rounding_test.cpp
    )
    target_link_libraries(gravia_tests PRIVATE gravia_core GTest::gtest_main pthread)
    target_compile_options(gravia_tests PRIVATE -fsanitize=address,undefined)
    target_link_options(gravia_tests PRIVATE -fsanitize=address,undefined)

    include(GoogleTest)
    gtest_discover_tests(gravia_tests)
else()
    message(WARNING "GoogleTest not found — unit tests will not be built. Install with: apt install libgtest-dev")
endif()

# ---------------------------------------------------------------------------
# Fuzz targets (clang + libFuzzer)
# ---------------------------------------------------------------------------
if(GRAVIA_ENABLE_FUZZ)
    if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        message(FATAL_ERROR "Fuzz targets require Clang (libFuzzer)")
    endif()

    add_executable(fuzz_decoder
        tests/fuzz/decoder_fuzz.cpp
    )
    target_link_libraries(fuzz_decoder PRIVATE gravia_core)
    target_compile_options(fuzz_decoder PRIVATE
        -fsanitize=fuzzer,address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(fuzz_decoder PRIVATE -fsanitize=fuzzer,address,undefined)
endif()

# ---------------------------------------------------------------------------
# Benchmarks (via Google Benchmark)
# ---------------------------------------------------------------------------
if(GRAVIA_ENABLE_BENCH)
    find_package(benchmark QUIET)
    if(benchmark_FOUND)
        add_executable(gravia_bench
            bench/decoder_bench.cpp
            bench/engine_bench.cpp
            bench/hashmap_bench.cpp
            bench/ringbuffer_bench.cpp
        )
        target_link_libraries(gravia_bench PRIVATE gravia_core benchmark::benchmark pthread)
        if(GRAVIA_NATIVE_MARCH)
            target_compile_options(gravia_bench PRIVATE -march=native -O3)
        endif()
    else()
        message(WARNING "Google Benchmark not found — benchmarks will not be built")
    endif()
endif()

# ---------------------------------------------------------------------------
# Installation
# ---------------------------------------------------------------------------
install(TARGETS gravia_engine_daemon DESTINATION bin)
install(DIRECTORY core/include/ DESTINATION include/gravia)

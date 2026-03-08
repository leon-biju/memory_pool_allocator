cmake_minimum_required(VERSION 4.1)
project(memory_pool_allocator)

set(CMAKE_CXX_STANDARD 23)

add_compile_options(
        -Wall
        -Wextra
        -Wpedantic
        $<$<CONFIG:Release>:-O2>
        $<$<CONFIG:Release>:-march=native>
        $<$<CONFIG:Debug>:-O0>
        $<$<CONFIG:Debug>:-g>
)

if(SANITIZE)
    add_compile_options(-fsanitize=${SANITIZE} -fno-omit-frame-pointer)
    add_link_options(-fsanitize=${SANITIZE})
endif()



include(FetchContent)
FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.zip
)

FetchContent_Declare(
        googlebenchmark
        URL https://github.com/google/benchmark/archive/refs/tags/v1.9.1.zip
)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest googlebenchmark)



add_library( memory_pool_lib INTERFACE )
target_include_directories(memory_pool_lib
        INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})


#Main executable
add_executable(memory_pool_allocator main.cpp)
target_link_libraries(memory_pool_allocator memory_pool_lib)

# Testing executable
enable_testing()
add_executable(memory_pool_allocator_tests tests/memory_pool_tests.cpp)
target_link_libraries(memory_pool_allocator_tests
        memory_pool_lib
        GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(memory_pool_allocator_tests)

#Benchmarks
add_executable(memory_pool_benchmarks benchmarks/bench_memory_pool.cpp)
target_link_libraries(memory_pool_benchmarks
        memory_pool_lib
        benchmark::benchmark
)
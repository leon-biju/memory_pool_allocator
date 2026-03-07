cmake_minimum_required(VERSION 4.1)
project(memory_pool_allocator)

set(CMAKE_CXX_STANDARD 23)

include(FetchContent)
FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.zip
)
FetchContent_MakeAvailable(googletest)
enable_testing()

add_library( memory_pool_lib INTERFACE )
target_include_directories(memory_pool_lib
        INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(memory_pool_allocator main.cpp)
target_link_libraries(memory_pool_allocator memory_pool_lib)

add_executable(memory_pool_allocator_tests tests/memory_pool_tests.cpp)
target_link_libraries(memory_pool_allocator_tests
        memory_pool_lib
        GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(memory_pool_allocator_tests)

cmake_minimum_required(VERSION 3.20)
project(finevox VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Options
option(FINEVOX_BUILD_TESTS "Build unit tests" ON)

# Main library
add_library(finevox STATIC
    src/position.cpp
    src/string_interner.cpp
    src/palette.cpp
    src/subchunk.cpp
    src/chunk_column.cpp
    src/coalescing_queue.cpp
    src/rotation.cpp
    src/world.cpp
    src/subchunk_manager.cpp
    src/batch_builder.cpp
    src/data_container.cpp
    src/serialization.cpp
    src/region_file.cpp
)

target_include_directories(finevox PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_options(finevox PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

# Tests
if(FINEVOX_BUILD_TESTS)
    enable_testing()

    # Fetch GoogleTest
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
    )
    FetchContent_MakeAvailable(googletest)

    add_executable(finevox_tests
        tests/test_position.cpp
        tests/test_string_interner.cpp
        tests/test_palette.cpp
        tests/test_subchunk.cpp
        tests/test_chunk_column.cpp
        tests/test_coalescing_queue.cpp
        tests/test_rotation.cpp
        tests/test_world.cpp
        tests/test_lru_cache.cpp
        tests/test_subchunk_manager.cpp
        tests/test_batch_builder.cpp
        tests/test_data_container.cpp
        tests/test_serialization.cpp
        tests/test_region_file.cpp
    )

    target_link_libraries(finevox_tests PRIVATE
        finevox
        GTest::gtest_main
    )

    include(GoogleTest)
    gtest_discover_tests(finevox_tests)
endif()

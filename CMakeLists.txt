cmake_minimum_required(VERSION 3.16)
project(driftwood VERSION 0.1.0 LANGUAGES CXX)

# C++17, not C++20
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Options
option(DRIFTWOOD_BUILD_TESTS "Build tests" ON)

# Enable testing
enable_testing()

# Find threading library (required by both Lazy SMP and the HTTP server)
find_package(Threads REQUIRED)

# Library
add_library(driftwood
    src/types.cpp
    src/board.cpp
    src/movegen.cpp
    src/perft.cpp
    src/tt.cpp
    src/eval.cpp
    src/search.cpp
    src/uci.cpp
    src/book.cpp
    src/syzygy.cpp
)
target_include_directories(driftwood PUBLIC include)
target_include_directories(driftwood PRIVATE src)

target_link_libraries(driftwood PUBLIC Threads::Threads)

# Set compiler flags
target_compile_options(driftwood PRIVATE
    $<$<CONFIG:Debug>:-Wall -Wextra -Wpedantic>
    $<$<CONFIG:Release>:-O3 -DNDEBUG -march=native>
)

# Binary (includes the HTTP server for the web UI)
add_executable(driftwood_cli src/main.cpp src/serve.cpp)
target_include_directories(driftwood_cli PRIVATE third_party)
target_link_libraries(driftwood_cli PRIVATE driftwood Threads::Threads)

# Ensure the binary is named 'driftwood'
set_target_properties(driftwood_cli PROPERTIES OUTPUT_NAME driftwood)

# Tests
if(DRIFTWOOD_BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/v1.14.0.zip
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    add_subdirectory(tests)
endif()

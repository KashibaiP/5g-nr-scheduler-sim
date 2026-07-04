cmake_minimum_required(VERSION 3.16)
project(5g-nr-scheduler-sim VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Compiler warnings
if(MSVC)
    add_compile_options(/W4 /WX)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

# Release build optimisations
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS_RELEASE "-O2")

# ── Library ──────────────────────────────────────────────────────────────────
add_library(scheduler_lib
    src/scheduler.cpp
    src/schedulers.cpp
)
target_include_directories(scheduler_lib PUBLIC include)

# ── Simulator executable ──────────────────────────────────────────────────────
add_executable(nr5g_scheduler src/main.cpp)
target_link_libraries(nr5g_scheduler PRIVATE scheduler_lib)

# ── Tests (optional, requires GoogleTest) ────────────────────────────────────
option(BUILD_TESTS "Build unit tests" OFF)
if(BUILD_TESTS)
    find_package(GTest REQUIRED)
    add_executable(scheduler_tests tests/test_scheduler.cpp)
    target_link_libraries(scheduler_tests PRIVATE scheduler_lib GTest::gtest_main)
    include(GoogleTest)
    gtest_discover_tests(scheduler_tests)
endif()

# ── Install ───────────────────────────────────────────────────────────────────
install(TARGETS nr5g_scheduler DESTINATION bin)
install(DIRECTORY include/ DESTINATION include/nr5g)

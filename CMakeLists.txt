# Copyright (c) 2026 Xavier Lee <kokteng1313@gmail.com>
# SPDX-License-Identifier: MIT

# 3.16 is what the features used here actually require: FetchContent_MakeAvailable
# (3.14) for the test suite's GoogleTest fallback, CONFIGURE_DEPENDS (3.12) in
# ClangFormat.cmake, and googletest v1.17's own declared minimum (3.16).
cmake_minimum_required(VERSION 3.16)
project(haidi_bms_cpp VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# clang-tidy and clangd both read this database.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(haidi_bms_cpp
    src/haidi_protocol.cpp
    src/haidi_frame_parser.cpp
    src/haidi_decode.cpp
    src/haidi_event.cpp
    src/haidi_bms.cpp
)

target_include_directories(haidi_bms_cpp PUBLIC include)

# The library targets embedded builds: no heap, no exceptions, no RTTI in its
# own code. -Wall -Wextra is what catches the missing-return class of bug that
# this codebase has shipped before.
target_compile_options(haidi_bms_cpp PRIVATE -Wall -Wextra -Wpedantic)

# Tests default to on for a standalone build and off when this project is
# pulled in with add_subdirectory(), so a consumer never fetches GoogleTest.
# PROJECT_IS_TOP_LEVEL would say this in one line but needs CMake 3.21.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(HAIDI_IS_TOP_LEVEL ON)
else()
    set(HAIDI_IS_TOP_LEVEL OFF)
endif()
option(HAIDI_BUILD_TESTS "Build the test suite" ${HAIDI_IS_TOP_LEVEL})
# Same default, for the same reason: the examples need POSIX termios, which a
# consumer's embedded or Windows toolchain may not have.
option(HAIDI_BUILD_EXAMPLES "Build the example programs" ${HAIDI_IS_TOP_LEVEL})

enable_testing()

if(HAIDI_BUILD_TESTS)
    add_subdirectory(tests)
endif()

if(HAIDI_BUILD_EXAMPLES)
    add_executable(haidi_poll_loop examples/poll_loop.cpp)
    target_link_libraries(haidi_poll_loop PRIVATE haidi_bms_cpp)
    target_compile_options(haidi_poll_loop PRIVATE -Wall -Wextra)

    # Hardware-in-the-loop dump of every read command. It needs a real BMS on a
    # serial port, so it lives here rather than in tests/, which must stay
    # runnable by ctest on a machine with no battery attached.
    add_executable(haidi_dump_all examples/dump_all.cpp)
    target_link_libraries(haidi_dump_all PRIVATE haidi_bms_cpp)
    target_compile_options(haidi_dump_all PRIVATE -Wall -Wextra)
endif()

include(cmake/ClangTidy.cmake)
haidi_enable_clang_tidy(haidi_bms_cpp)

include(cmake/ClangFormat.cmake)

include(cmake/Doxygen.cmake)

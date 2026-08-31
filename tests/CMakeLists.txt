# Copyright (c) 2026 Xavier Lee <kokteng1313@gmail.com>
# SPDX-License-Identifier: MIT

# GoogleTest: prefer a copy the system already provides, fall back to fetching a
# pinned tag. The find_package branch is what keeps packaged and offline builds
# working; the FetchContent branch is what makes a bare clone build first time.
find_package(GTest QUIET)

if(NOT TARGET GTest::gtest_main)
    message(STATUS "GTest not found; fetching googletest v1.17.0.")
    include(FetchContent)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.17.0
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(googletest)
endif()

add_executable(haidi_tests
    test_protocol.cpp
    test_frame_parser.cpp
    test_payloads.cpp
    test_event.cpp
    test_session.cpp
    test_commands.cpp
    test_multiframe.cpp
)

target_link_libraries(haidi_tests PRIVATE haidi_bms_cpp GTest::gtest_main)

# This suite tests the public interface, so it gets exactly what a consumer of
# the library gets: include/, by way of haidi_bms_cpp's PUBLIC include
# directory. src/ is deliberately absent -- an #include <haidi_decode.hpp> here
# must fail to compile, which is what stops these tests drifting into internals.
target_include_directories(haidi_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# -Wpedantic is omitted where the library has it: GoogleTest's own headers do
# not compile cleanly under it.
target_compile_options(haidi_tests PRIVATE -Wall -Wextra)

# Registers every TEST individually with CTest, rather than one lumped entry.
include(GoogleTest)
gtest_discover_tests(haidi_tests)

# Copyright (c) 2026 Xavier Lee <kokteng1313@gmail.com>
# SPDX-License-Identifier: MIT

# clang-format integration.
#
#   cmake --build build --target format         -- rewrite sources in place
#   cmake --build build --target format-check   -- fail if anything is unformatted
#
# Both read the style from the top-level .clang-format file.

find_program(CLANG_FORMAT_EXE NAMES clang-format clang-format-21 clang-format-20 clang-format-19)

if(NOT CLANG_FORMAT_EXE)
    message(STATUS "clang-format not found; `format` targets unavailable.")
    return()
endif()

file(GLOB_RECURSE HAIDI_FORMAT_SOURCES
    CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/*.hpp"
    "${CMAKE_SOURCE_DIR}/include/*.hpp"
    "${CMAKE_SOURCE_DIR}/tests/*.cpp"
    "${CMAKE_SOURCE_DIR}/tests/*.hpp"
    "${CMAKE_SOURCE_DIR}/examples/*.cpp"
)

add_custom_target(format
    COMMAND "${CLANG_FORMAT_EXE}" -i --style=file ${HAIDI_FORMAT_SOURCES}
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Formatting sources with clang-format"
    VERBATIM
)

add_custom_target(format-check
    COMMAND "${CLANG_FORMAT_EXE}" --dry-run --Werror --style=file ${HAIDI_FORMAT_SOURCES}
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Checking source formatting with clang-format"
    VERBATIM
)

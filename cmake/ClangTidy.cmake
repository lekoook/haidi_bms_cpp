# clang-tidy integration.
#
# Two ways to lint:
#   1. `cmake --build build --target lint`  -- lints the whole compile database.
#   2. `cmake -B build -DHAIDI_ENABLE_CLANG_TIDY=ON` -- runs clang-tidy inline
#      on every translation unit as part of the normal build.
#
# Both read the checks from the top-level .clang-tidy file.

option(HAIDI_ENABLE_CLANG_TIDY "Run clang-tidy as part of the build" OFF)
option(HAIDI_CLANG_TIDY_WERROR "Treat clang-tidy diagnostics as build errors" OFF)

find_program(CLANG_TIDY_EXE NAMES clang-tidy clang-tidy-21 clang-tidy-20 clang-tidy-19)
find_program(RUN_CLANG_TIDY_EXE NAMES run-clang-tidy run-clang-tidy-21 run-clang-tidy-20)

# Attaches clang-tidy to a target's compile steps when HAIDI_ENABLE_CLANG_TIDY is on.
function(haidi_enable_clang_tidy target)
    if(NOT HAIDI_ENABLE_CLANG_TIDY)
        return()
    endif()

    if(NOT CLANG_TIDY_EXE)
        message(WARNING "HAIDI_ENABLE_CLANG_TIDY=ON but clang-tidy was not found; skipping.")
        return()
    endif()

    set(tidy_cmd "${CLANG_TIDY_EXE}" "--quiet")
    if(HAIDI_CLANG_TIDY_WERROR)
        list(APPEND tidy_cmd "--warnings-as-errors=*")
    endif()

    set_target_properties(${target} PROPERTIES CXX_CLANG_TIDY "${tidy_cmd}")
endfunction()

# Standalone `lint` / `lint-fix` targets over the whole compile database.
if(RUN_CLANG_TIDY_EXE)
    add_custom_target(lint
        COMMAND "${RUN_CLANG_TIDY_EXE}"
                -p "${CMAKE_BINARY_DIR}"
                -quiet
                "${CMAKE_SOURCE_DIR}/src/.*"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Running clang-tidy over src/"
        USES_TERMINAL
    )

    add_custom_target(lint-fix
        COMMAND "${RUN_CLANG_TIDY_EXE}"
                -p "${CMAKE_BINARY_DIR}"
                -quiet
                -fix
                "${CMAKE_SOURCE_DIR}/src/.*"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Running clang-tidy over src/ and applying fixes"
        USES_TERMINAL
    )
elseif(CLANG_TIDY_EXE)
    message(STATUS "run-clang-tidy not found; `lint` target unavailable (clang-tidy itself is present).")
endif()

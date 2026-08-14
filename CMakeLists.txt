cmake_minimum_required(VERSION 3.10.0)
project(haidi_bms_cpp VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# clang-tidy and clangd both read this database.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(haidi_bms_cpp
    src/haidi_bms_cpp.cpp
    src/HaidiBMS.cpp
)

target_include_directories(haidi_bms_cpp PUBLIC include)

include(cmake/ClangTidy.cmake)
haidi_enable_clang_tidy(haidi_bms_cpp)

include(cmake/ClangFormat.cmake)

cmake_minimum_required(VERSION 3.21)

project(OpenArchitect
    VERSION 0.1.0
    DESCRIPTION "Open source residential design with structural load takedown"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(OA_BUILD_APP "Build the Qt desktop application" ON)
option(OA_BUILD_TESTS "Build the unit tests" ON)

if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Dependencies)

add_subdirectory(src/core)
add_subdirectory(src/structural)

if(OA_BUILD_APP)
    add_subdirectory(src/app)
endif()

if(OA_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

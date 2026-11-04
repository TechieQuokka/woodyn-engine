cmake_minimum_required(VERSION 3.30)

project(woodyn
    VERSION 0.1.0
    DESCRIPTION "WOODYN - Real Wood. Real Physics."
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

option(WOODYN_BUILD_TESTS  "Build unit tests"            ON)
option(WOODYN_BUILD_VIEWER "Build the SDL3 debug viewer" ON)
option(WOODYN_WARNINGS_AS_ERRORS "Treat warnings as errors for WOODYN targets" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)
include(Dependencies)

add_subdirectory(engine)
add_subdirectory(apps/bench)

if(WOODYN_BUILD_VIEWER)
    add_subdirectory(apps/viewer)
endif()

if(WOODYN_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

cmake_minimum_required(VERSION 3.5)

# Project name and version
project(timpani-o VERSION 0.1.0 DESCRIPTION "Timpani-O")

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)

# Set cmake module path
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Add the binary dir to include dirs for gRPC/protobuf generated files
include_directories(${CMAKE_CURRENT_BINARY_DIR})

# Add proto directory
add_subdirectory(proto)

# Add source directory
add_subdirectory(src)

# Add libtrpc submodule
add_subdirectory(${CMAKE_SOURCE_DIR}/external/libtrpc EXCLUDE_FROM_ALL)

# Option to build tests
option(BUILD_TESTS "Build unit tests" OFF)

# Add tests directory if building tests
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Packaging configuration
set(CPACK_PACKAGE_NAME "timpani-o")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_CONTACT "Timpani Developers")
include(CPack)

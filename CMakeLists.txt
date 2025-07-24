cmake_minimum_required(VERSION 3.5)

# Project name and version
project(timpani-o VERSION 0.1.0 DESCRIPTION "Timpani-O")

# Set C++ standard
set(CMAKE_CXX_STANDARD 14)

# Set cmake module path
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Add the binary dir to include dirs for gRPC/protobuf generated files
include_directories(${CMAKE_CURRENT_BINARY_DIR})

# Add proto directory
add_subdirectory(proto)

# Add source directory
add_subdirectory(src)

# Add libtrpc submodule
add_subdirectory(${CMAKE_SOURCE_DIR}/external/libtrpc)

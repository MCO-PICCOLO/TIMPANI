# Project Overview

This `Timpani-O` project is a C++ application that interacts with a time-triggered scheduling system for real-time tasks. It includes a client module that can receive scheduling information from a workload orchestrator, aka. `Piccolo`, and a server module that can provide scheduling tables and task information to the client on each computer node.

## Folder Structure

- `src/`: Contains the source code for the client and server modules.
- `proto/`: Contains the Protocol Buffers definitions used for gRPC communication with the workload orchestrator.
- `cmake/`: Contains CMake modules for building the project.

## Libraries and Dependencies

- CMake: For building the project.
- gRPC: For communication between `Timpani-O` and `Piccolo`.
- Protocol Buffers: For serializing structured data.

## Coding Style
- Follow the part of Microsoft's C++ coding style guide
  - refer to [Coding style](../README.md#coding-style) section in the README.md file.

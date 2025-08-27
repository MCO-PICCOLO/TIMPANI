# TIMPANI-O

## Getting started

Refer to [This README.md](http://mod.lge.com/hub/timpani/time-trigger/-/blob/main/README.md) for the full description of the project.

## Prerequisites

- On Ubuntu
  - gRPC & protobuf
    ```
    sudo apt install -y libgrpc++-dev libprotobuf-dev protobuf-compiler-grpc
    ```
  - Prerequisites for libtrpc(D-Bus)
    ```
    sudo apt install -y libsystemd-dev
    ```

- On CentOS
  - gRPC & protobuf
    ```
    sudo dnf install -y protobuf-devel protobuf-compiler
    # Enable EPEL(Extra Packages for Enterprise Linux) repository for gRPC
    sudo dnf install -y epel-release
    sudo dnf install -y grpc-devel
    ```
  - Prerequisites for libtrpc(D-Bus)
    ```
    sudo dnf install -y systemd-devel
    ```

## How to build

```
git clone --recurse-submodules http://mod.lge.com/hub/timpani/timpani-o.git
cd timpani-o
mkdir build
cd build
cmake ..
make
```

### Cross-compilation for ARM64

```
cmake -DCMAKE_TOOLCHAIN_FILE=../cmake/toolchain-aarch64-gcc.cmake ..
```

## Coding style

- TIMPANI-O follows the [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html) with some modifications:
  - Use 4 spaces for indentation.
  - Place a line break before the opening brace after function and class definitions
- Use `clang-format` to format your code with .clang-format file provided in the project root.
  ```
  clang-format -i <file>
  ```
- `.clang-format` and `.editorconfig` are provided to help maintain consistent coding styles.

## How to run

- To run Timpani-O with default options:
  ```
  timpani-o
  ```
- To run Timpani-O with specific options, refer to the help message:
  ```
  timpani-o -h
  ```

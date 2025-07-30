# TIMPANI-O

## Getting started

Refer to [This README.md](http://mod.lge.com/hub/timpani/time-trigger/-/blob/main/README.md) for the full description of the project.

## Prerequisites

- gRPC & protobuf

```
sudo apt install -y libgrpc++-dev protobuf-compiler-grpc
```

## Build

```
git clone --recurse-submodules http://mod.lge.com/hub/timpani/timpani-o.git
cd timpani-o
mkdir build
cd build
cmake ..
make
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

To be defined

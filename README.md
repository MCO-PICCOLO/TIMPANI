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

- TIMPANI-O follows part of the Microsoft C++ Style Guide
  - Use 4 spaces for indentation.
  - Use [`Pascal case`](https://en.wiktionary.org/wiki/Pascal_case) or [`upper camel case`](https://en.wikipedia.org/wiki/Camel_case) for function and class names.
  - Use [`snake case`](https://en.wikipedia.org/wiki/Snake_case) for variable names.
- Use `clang-format` to format your code.
  ```
  clang-format --style=microsoft -i <file>
  ```
- `.editorconfig` is provided to help maintain consistent coding styles.

## How to run

To be defined

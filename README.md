# libtrpc (Timpani RPC Library)

## How to build

### Prerequisites
- Make sure to have pkg-config and libsystemd-dev installed.
```
sudo apt install pkg-config
sudo apt install libsystemd-dev
```

### Compiling for local host
```
mkdir -p build-x86
cd build-x86
cmake ..
make
```

### Cross-compiling for ARM64
```
mkdir -p build-arm64
cd build-arm64
cmake -DCMAKE_TOOLCHAIN_FILE=../aarch64-toolchain.cmake ..
make
```

### Cross-compiling for ARM32
```
mkdir -p build-arm32
cd build-arm32
cmake -DCMAKE_TOOLCHAIN_FILE=../armhf-toolchain.cmake ..
make
```

## Running test programs

- The sample server program waits for connection from clients and provides several methods.

```
./trpc_server
```

- The sample client program connects to the server and calls "Register" and "SchedInfo" methods.
```
./trpc_client
```

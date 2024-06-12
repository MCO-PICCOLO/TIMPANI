# libtrpc (Timpani RPC Library)

## How to build

```
mkdir build-x86
cd build-x86
cmake .
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

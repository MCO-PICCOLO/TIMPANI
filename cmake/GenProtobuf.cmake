find_package(gRPC REQUIRED)
find_package(Protobuf REQUIRED)

include_directories(${Protobuf_INCLUDE_DIRS})

macro(GEN_PROTOBUF ProtoFile GenFiles ProtoLibs)
  protobuf_generate(
    OUT_VAR _proto_srcs
    LANGUAGE cpp
    PROTOS ${ProtoFile}
  )

  protobuf_generate(
    OUT_VAR _grpc_srcs
    LANGUAGE grpc
    GENERATE_EXTENSIONS .grpc.pb.h .grpc.pb.cc
    PLUGIN "protoc-gen-grpc=\$<TARGET_FILE:gRPC::grpc_cpp_plugin>"
    PROTOS ${ProtoFile}
  )

  set(${GenFiles} ${_proto_srcs} ${_grpc_srcs})
  set(${ProtoLibs} gRPC::grpc++ protobuf::libprotobuf)
endmacro()

/*
SPDX-FileCopyrightText: Copyright 2026 LG Electronics Inc.
SPDX-License-Identifier: MIT
*/

/// Build script – compiles protobuf definitions into Rust source code.
///
/// tonic-build wraps prost-build and additionally generates tonic server/client
/// stubs.  The generated files are written to `OUT_DIR` (managed by Cargo) and
/// pulled into the crate via `tonic::include_proto!` in `src/proto/mod.rs`.
///
/// Prerequisites
/// -------------
/// `protoc` (the protobuf compiler) must be available on `$PATH`, or its path
/// must be set in the `PROTOC` environment variable before running `cargo build`.
/// Install on Ubuntu/Debian: `sudo apt install -y protobuf-compiler`
/// Install on macOS:          `brew install protobuf`

fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Path to the proto source relative to this crate's root
    // (rust/timpani-o/ → ../../timpani-o/proto/)
    let proto_root = "../../timpani-o/proto";
    let proto_file = format!("{}/schedinfo.proto", proto_root);

    // Tell Cargo to re-run this build script when the proto file changes
    println!("cargo:rerun-if-changed={}", proto_file);

    tonic_build::configure()
        // Generate both server (SchedInfoService) and client (FaultService) stubs
        .build_server(true)
        .build_client(true)
        // Derive serde Serialize/Deserialize on every generated message so we can
        // (de)serialise them easily in tests and logging.
        .type_attribute(".", "#[derive(serde::Serialize, serde::Deserialize)]")
        .compile_protos(
            &[proto_file.as_str()], // proto files to compile
            &[proto_root],          // directories to search for imports
        )?;

    Ok(())
}

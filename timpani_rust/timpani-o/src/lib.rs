/*
SPDX-FileCopyrightText: Copyright 2026 LG Electronics Inc.
SPDX-License-Identifier: MIT
*/

//! Timpani-O – global scheduler (Rust port)
//!
//! Module layout (filled in as the migration progresses):
//!
//! ```text
//! lib.rs
//! ├── proto/          – generated gRPC/protobuf types & stubs
//! ├── config/         – YAML node configuration
//! ├── scheduler/      – three scheduling algorithms
//! ├── hyperperiod/    – LCM / GCD helpers
//! ├── grpc/           – gRPC server + client wiring  (pending)
//! └── fault/          – fault reporting to Pullpiri  (pending)
//! ```

pub mod config;
pub mod hyperperiod;
pub mod proto;
pub mod scheduler;
pub mod task;

// Placeholders – uncommented as each module is implemented
// pub mod grpc;
// pub mod fault;

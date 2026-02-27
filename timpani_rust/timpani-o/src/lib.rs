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
//! ├── config/         – YAML node configuration (Week 1)
//! ├── scheduler/      – three scheduling algorithms  (Week 1)
//! ├── hyperperiod/    – LCM / GCD helpers            (Week 1)
//! ├── grpc/           – gRPC server + client wiring  (Week 2)
//! └── fault/          – fault reporting to Piccolo   (Week 2)
//! ```

pub mod config;
pub mod hyperperiod;
pub mod proto;
pub mod scheduler;
pub mod task;

// Placeholders – uncommented as each module is implemented
// pub mod grpc;
// pub mod fault;

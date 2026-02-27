/*
SPDX-FileCopyrightText: Copyright 2026 LG Electronics Inc.
SPDX-License-Identifier: MIT
*/

//! Structured error types for the Timpani-O global scheduler.
//!
//! Two error enums model the two failure layers:
//!
//! * [`AdmissionReason`] — why a single task was rejected by a specific node
//!   (low-level, carries exact resource values).
//! * [`SchedulerError`] — top-level failure returned from
//!   [`GlobalScheduler::schedule()`](super::GlobalScheduler::schedule).
//!
//! # Automotive note
//! Every variant carries enough structured data to:
//! 1. Emit a fully-qualified `tracing` event (task name, node, values).
//! 2. Generate a DTC / DEM event when the fault reporting proto is extended.
//! 3. Be matched by the gRPC handler to map to an appropriate `tonic::Status`
//!    code.
//!
//! **Do not** replace these with `anyhow::Error` in production paths — the
//! structured variants are intentional.

use thiserror::Error;

// ── Admission control ─────────────────────────────────────────────────────────

/// Detailed reason why a task was rejected during admission control.
///
/// Carried inside [`SchedulerError::AdmissionRejected`] so the caller always
/// knows both *which* task/node pair failed and *why*.
#[derive(Debug, Clone, PartialEq)]
pub enum AdmissionReason {
    /// The node name is not present in the loaded [`NodeConfigManager`].
    ///
    /// [`NodeConfigManager`]: crate::config::NodeConfigManager
    NodeNotFound { node: String },

    /// Task memory requirement exceeds the node's configured maximum.
    ///
    /// Dormant until the proto `TaskInfo` message carries a `memory_mb` field.
    /// When `task.memory_mb == 0` this variant is never produced.
    InsufficientMemory { required_mb: u64, available_mb: u64 },

    /// The CPU requested by a `CpuAffinity::Pinned` mask is not in the node's
    /// CPU set.
    CpuAffinityUnavailable { requested_cpu: u32 },

    /// Assigning the task to this CPU would push its utilisation above the
    /// `CPU_UTILIZATION_THRESHOLD`.
    CpuUtilizationExceeded {
        cpu: u32,
        current: f64,
        added: f64,
        threshold: f64,
    },

    /// The node has no CPU with enough headroom to accommodate the task, even
    /// after considering all CPUs.
    NoAvailableCpu,
}

impl std::fmt::Display for AdmissionReason {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            AdmissionReason::NodeNotFound { node } => {
                write!(f, "node '{}' not found in configuration", node)
            }

            AdmissionReason::InsufficientMemory {
                required_mb,
                available_mb,
            } => write!(
                f,
                "task requires {}MB but node only has {}MB available",
                required_mb, available_mb
            ),

            AdmissionReason::CpuAffinityUnavailable { requested_cpu } => write!(
                f,
                "pinned CPU {} is not in this node's CPU set",
                requested_cpu
            ),

            AdmissionReason::CpuUtilizationExceeded {
                cpu,
                current,
                added,
                threshold,
            } => write!(
                f,
                "CPU {} utilization would be {:.1}% + {:.1}% = {:.1}% (threshold {:.0}%)",
                cpu,
                current * 100.0,
                added * 100.0,
                (current + added) * 100.0,
                threshold * 100.0,
            ),

            AdmissionReason::NoAvailableCpu => write!(
                f,
                "no CPU on this node can accommodate the task utilization"
            ),
        }
    }
}

// ── Top-level scheduler errors ────────────────────────────────────────────────

/// Top-level error type returned by
/// [`GlobalScheduler::schedule()`](super::GlobalScheduler::schedule).
///
/// Every variant is named to clearly indicate *what* went wrong so the gRPC
/// handler can map them to appropriate `tonic::Status` codes:
///
/// | Variant | Suggested gRPC status |
/// |---|---|
/// | `NoTasks` | `InvalidArgument` |
/// | `ConfigNotLoaded` | `FailedPrecondition` |
/// | `UnknownAlgorithm` | `InvalidArgument` |
/// | `MissingWorkloadId` / `MissingTargetNode` | `InvalidArgument` |
/// | `AdmissionRejected` | `ResourceExhausted` |
/// | `NoSchedulableNode` | `ResourceExhausted` |
#[derive(Debug, Error)]
pub enum SchedulerError {
    /// `schedule()` was called with an empty task list.
    #[error("no tasks provided — task list is empty")]
    NoTasks,

    /// [`NodeConfigManager`] has not been loaded (no YAML file parsed yet).
    ///
    /// [`NodeConfigManager`]: crate::config::NodeConfigManager
    #[error("node configuration is not loaded")]
    ConfigNotLoaded,

    /// The `algorithm` string passed to `schedule()` is not recognised.
    #[error("unknown scheduling algorithm: '{0}' (valid: target_node_priority, least_loaded, best_fit_decreasing)")]
    UnknownAlgorithm(String),

    /// A task arrived without a `workload_id` field set.
    ///
    /// Every task must carry a workload identifier — it is required by the
    /// `target_node_priority` algorithm and for fault reporting.
    #[error("task '{task}' has no workload_id — all tasks must carry a workload identifier")]
    MissingWorkloadId { task: String },

    /// A task arrived without a `target_node` field set, which is required by
    /// the `target_node_priority` algorithm.
    #[error("task '{task}' has no target_node — required by target_node_priority algorithm")]
    MissingTargetNode { task: String },

    /// Admission control rejected a task for a specific node with a detailed
    /// reason.
    ///
    /// The `reason` field carries exact resource values (memory MB, CPU
    /// utilization percentages) so the caller can log or forward them without
    /// further parsing.
    #[error("task '{task}' rejected by node '{node}': {reason}")]
    AdmissionRejected {
        task: String,
        node: String,
        reason: AdmissionReason,
    },

    /// No node in the configuration could accept the task (all nodes either
    /// failed admission or had no headroom).
    #[error("no schedulable node found for task '{task}'")]
    NoSchedulableNode { task: String },
}

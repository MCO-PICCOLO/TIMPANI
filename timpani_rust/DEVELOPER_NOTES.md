<!--
SPDX-FileCopyrightText: Copyright 2026 LG Electronics Inc.
SPDX-License-Identifier: MIT
-->
# Timpani-O Rust Port — Developer Notes

This document records design decisions, deliberate departures from the C++
implementation, and the reasoning behind them.  Keep it updated as new
modules are implemented.

---

## Project context

Timpani-O is the global scheduler component of the Timpani real-time task
scheduling system.  It receives a workload description from Piccolo over gRPC,
schedules the tasks across compute nodes, and forwards the schedule to
Timpani-N nodes (also over gRPC).

The Rust port targets **automotive-grade production quality**:
ISO 26262 / AUTOSAR AP practices are applied throughout.

---

## Module status

| Module | Status | Notes |
|---|---|---|
| `proto/` | ✅ Week 1 | Generated from `schedinfo.proto` via `tonic-build` |
| `config/` | ✅ Week 1 | 10 tests passing |
| `task/` | ✅ Week 1 | 16 tests passing |
| `hyperperiod/` | ✅ Week 1 | 30 tests passing (math + manager) |
| `scheduler/` | ✅ Week 1 | All three algorithms, L&L check, 12 tests |
| `grpc/` | ⏳ Week 2 | tonic server wiring |
| `fault/` | ⏳ Week 2 | FaultService gRPC client to Piccolo |

---

## Decision log

### D-001 — No D-Bus; pure gRPC throughout (Week 1)

**C++ behaviour**: The C++ `main.cpp` used `-d/--dbusport` (default 7777) for
downstream communication to Timpani-N nodes via D-Bus.

**Change**: Renamed the flag to `-d/--nodeport` (default 50054).  All
downstream communication is gRPC.  D-Bus is not used anywhere in the Rust
port.

**Reason**: Architectural decision by the team.  gRPC is already used
upstream (Piccolo → Timpani-O); using it downstream as well eliminates a
second IPC mechanism and simplifies deployment.

---

### D-002 — Single timing unit: microseconds only (Week 1)

**C++ behaviour**: `task.h` carried three parallel timing representations —
`period_us` / `runtime_us` / `deadline_us` (microseconds, `uint64_t`),
`execution_time` / `deadline` / `period` (milliseconds, `int`), and
`release_time` (milliseconds, `int`).

**Change**: `Task` keeps only `*_us` fields.  `SchedTask` (the wire-ready
output) converts to nanoseconds at the boundary via `saturating_mul(1_000)`.

**Reason**: Duplicate fields invite divergence bugs.  The proto uses
microseconds; Timpani-N expects nanoseconds.  One conversion point is
easier to audit.

---

### D-003 — `memory_mb` reinstated in `Task` and `NodeConfig` (Week 1)

**C++ behaviour**: `task.h` had `int memory_mb` (defaulted to 64).  It was
never populated from the proto, making admission control a silent no-op.

**Initial Rust draft**: Field was dropped as "dead".

**Correction**: Reinstated as `u64`.  Zero means "no constraint" (dormant
until the proto `TaskInfo` message carries the field).  `max_memory_mb` in
`NodeConfig` is `u64::MAX` when absent from YAML.

**Reason**: In mixed-criticality automotive systems (ISO 26262) memory
budgets are required for ASIL-D / QM partitioning.  The field must exist in
the pipeline now so adding proto support later is a non-breaking change.

---

### D-004 — `max_memory_mb` type changed from `u32` to `u64` (Week 1)

**C++ behaviour**: `node_config.h` had `int max_memory_mb`.

**Change**: `u64` in both `NodeConfigEntry` (serde) and `NodeConfig` (public).
The YAML `#[serde(default)]` returns `u64::MAX` when the field is absent,
meaning "unconstrained" — existing YAML files without the field continue to
parse without change.

**Reason**: Consistency with `Task::memory_mb`.  `u32` caps at ~4 GB; future
high-memory ECU nodes may exceed this.

---

### D-005 — `CpuAffinity` as a `u64` bitmask enum (Week 1)

**C++ behaviour**: Two parallel representations — `std::string affinity`
(e.g. `"3"` or `"any"`) plus `int cpu_affinity` (-1 for any).  Single CPU
only; parsed with `std::stoi` which can throw.

**Change**: `CpuAffinity::Any` or `CpuAffinity::Pinned(u64)`.  The `u64` is
a bitmask (bit N set = CPU N allowed).  `lowest_cpu()` extracts the first set
bit, matching current Timpani-N behaviour.

**Reason**: The proto `cpu_affinity` field is already `uint64`.  A bitmask
supports multi-CPU affinity (future) without a breaking API change.
Eliminates `stoi`-throws and the dual-field inconsistency.

---

### D-006 — `SchedPolicy` enum carried through the whole pipeline (Week 1)

**C++ behaviour**: `task.h` stored policy as a bare `int`.  Validation only
happened at the proto decode boundary; an invalid integer could silently
travel through the entire scheduler.

**Change**: `SchedPolicy` enum (`Normal`, `Fifo`, `RoundRobin`).  Unknown
proto integers are mapped to `Normal` at decode time.  `to_linux_int()`
converts back to the integer only at the Timpani-N wire boundary.

**Reason**: Makes invalid policy values impossible to represent inside
Timpani-O.  Each conversion point is explicit and auditable.

---

### D-007 — Unified per-CPU utilisation model for all three algorithms (Week 1)

**C++ behaviour**: `target_node_priority` used a per-CPU utilisation map
(multiple tasks per CPU, up to threshold).  `least_loaded` and
`best_fit_decreasing` used a CPU-pool dequeue model (one task per CPU slot).
This was an accidental inconsistency — `target_node_priority` was written
later.

**Change**: All three algorithms use per-CPU utilisation tracking.  CPUs are
never removed from the available pool; tasks are admitted as long as
`Σ(runtime/period) ≤ CPU_UTILIZATION_THRESHOLD` on each CPU.

**Reason**: Correct real-time model.  Multiple periodic tasks sharing a core
is the standard RT scenario.  The pool-dequeue model wasted cores.

---

### D-008 — Stateless `schedule()`: no mutable struct fields (Week 1)

**C++ behaviour**: `GlobalScheduler` stored `available_cpus_per_node_`,
`cpu_utilization_per_node_`, `sched_info_map_`, and `tasks_` as mutable
member variables.  Callers had to call `clear()` between runs.

**Change**: `GlobalScheduler` holds only `Arc<NodeConfigManager>` (immutable
shared config).  All per-run state is allocated inside `schedule()` and
dropped at return.

**Reason**:
1. Eliminates the `clear()` footgun (forgetting it = stale state).
2. `GlobalScheduler` is `Send + Sync` — the gRPC handler can hold one behind
   an `Arc` and call `schedule()` concurrently from multiple async tasks.
3. `tokio::task::spawn_blocking` can take the scheduler by value without
   `Arc<Mutex<>>` wrapping.

---

### D-009 — `BTreeMap` instead of `HashMap` for all node/CPU maps (Week 1)

**C++ behaviour**: `std::map` (sorted).

**Rust draft**: Initially used `HashMap` (standard Rust idiom).

**Correction**: Changed to `BTreeMap` for `AvailCpus` and `CpuUtil`.

**Reason**: `HashMap` in Rust uses a random seed (SipHash with random salt).
Iteration order changes between runs.  For automotive:
- **Repeatability**: same input must always produce same schedule.
- **Auditability**: post-incident reconstruction must be deterministic.
- **Test reliability**: no flaky tests due to map ordering.

`BTreeMap` iterates alphabetically by key; performance difference is
negligible for 3–10 nodes.

---

### D-010 — Structured error types via `thiserror` (Week 1)

**C++ behaviour**: `schedule()` returned `bool`.  Failure reasons were logged
as strings.

**Change**: `SchedulerError` enum with typed variants (`NoTasks`,
`ConfigNotLoaded`, `UnknownAlgorithm`, `MissingWorkloadId`, `MissingTargetNode`,
`AdmissionRejected { task, node, reason: AdmissionReason }`,
`NoSchedulableNode`).  `AdmissionReason` carries exact resource values
(MB, utilisation percentages).

**Reason**: ISO 26262 requires every fault to be uniquely identifiable and
carry enough data for post-mortem analysis and DTC generation.  The gRPC
handler maps variants to `tonic::Status` codes.  When the `FaultService`
proto is extended, the structured data is already available.

**Current limitation**: `FaultService` in the proto only handles deadline
miss events from Timpani-N; it does not yet carry scheduler errors.  The
structured errors are **logged** for now.  Proto extension is pending.

---

### D-011 — `get_sorted_cpus_by_utilization` kept as `sorted_cpus()` (Week 1)

**C++ behaviour**: Function existed in the header but was never called.

**Initial proposal**: Drop it as dead code.

**Correction**: Kept, renamed `sorted_cpus(node_id, avail, util, prefer_high_util)`.

**Reason**: The `prefer_high_util` flag represents two real strategies:
- `true` → bin-packing / consolidation (idle cores can be power-gated via
  DVFS — relevant for AUTOSAR EEM).
- `false` → spreading / load balancing (reduces thermal gradients on SoC —
  relevant for thermal management on R-Car, TDA4, S32G).

Neither is dead code; both are building blocks for future algorithm variants.

---

### D-012 — Liu & Layland post-schedule feasibility check (Week 1)

**C++ behaviour**: No theoretical feasibility check.  Only the 90 % per-CPU
utilisation heuristic.

**Change**: After every `schedule()` call, per-node task sets are checked
against the Liu & Layland bound:

```
U_bound(n) = n × (2^(1/n) − 1)
```

Exceeding the bound emits a `warn!` log event.  The schedule is **still
returned** — this is a warning, not an error.

**Status**: Implemented in `src/scheduler/feasibility.rs`.  Pending
management approval to use the L&L bound as a dynamic `CPU_UTILIZATION_THRESHOLD`
instead of the fixed 90 % heuristic.

**Current effective threshold**: 90 % (hard-coded in `CPU_UTILIZATION_THRESHOLD`).

---

### D-013 — `memory_mb == 0` means "unconstrained" (not a real zero) (Week 1)

**Reason**: The proto `TaskInfo` message does not yet carry a `memory_mb`
field.  Until it does, all tasks arrive with `memory_mb == 0`.  Treating zero
as "no constraint" means admission control's memory check is dormant by
default and activates automatically once the proto is extended — no code
changes needed at that point.

---

### D-014 — `is_task_schedulable_on_node` collapsed into `check_admission` (Week 1)

**C++ behaviour**: Separate function returning `bool`.

**Change**: Replaced by `check_admission() -> Result<(), AdmissionReason>`.

**Reason**: `bool` return is insufficient for automotive fault reporting (see
D-010).  The function previously allowed scheduling when `node_config_manager_`
was null (`return true`) — a silent footgun.  The Rust version panics at
construction time if `Arc<NodeConfigManager>` is null (impossible in safe Rust).

---

### D-015 — Node utilisation derived from CPU util map, not task re-scan (Week 1)

**C++ behaviour**: `calculate_node_utilization()` scanned all tasks checking
`task.assigned_node == node_id` — O(tasks × nodes).

**Change**: `calculate_node_utilization()` sums the per-CPU utilisation map
entries for the node — O(CPUs per node).

**Reason**: Eliminates two sources of truth that can diverge.  The CPU util
map is already being maintained as tasks are assigned; summing it is always
consistent.

---

## Known limitations / future work

- **Single workload per `schedule()` call**: `initialize_available_cpus()`
  resets all CPUs on each call.  Cross-workload CPU reservation requires a
  persistent state store (outside the scope of Week 1).

- **`FaultService` proto gap**: Scheduler errors cannot be forwarded to
  Piccolo yet.  Requires a new `SchedulerFault` message in `schedinfo.proto`.

- **Liu & Layland threshold**: Currently informational only.  Management
  approval needed before using it as a dynamic `CPU_UTILIZATION_THRESHOLD`.

- **Response Time Analysis (RTA)**: L&L gives a sufficient condition; RTA
  gives the exact bound.  Implementing RTA is a future Week 2+ task.

- **Cluster / SoC affinity**: Heterogeneous cores (A72 vs R5 etc.) are not
  modelled yet.  The `cluster_requirement` field from C++ was dropped; it can
  be reinstated as an enum when needed.

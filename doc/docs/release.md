
# Release Plan: Migration of Timpani-O Core (C++ to Rust Parity)

**Epic Owner:** @jayakrishnan04b
**Status:** In Progress
**Goal:** Achieve functional parity for the Timpani-O global scheduler by migrating from C++ (D-Bus) to Rust (gRPC).

---

## Overview

This release tracks the complete migration of the Timpani-O global scheduler from C++ to Rust. The primary objective is to match the reference C++ implementation's functionality while transitioning from D-Bus to gRPC for all communication.

**Reference Implementation:**
- C++ Source: `timpani-o/src/`
- Protocols: Moving from D-Bus to `proto/schedinfo.proto`

---

## High-Level Phases

1. **Core Logic & Scheduling Algorithms**
2. **gRPC Connectivity Layer (Piccolo & Node Interfaces)**
3. **Validation & Parallel Testing**
4. **Production Readiness**

---

## Phase 1: Core Scheduling Logic and Foundation

**Description:**
Port the core engine and all primary scheduling algorithms from C++ to Rust.

**Technical Tasks:**
- Project Setup: Initialize Cargo.toml with tokio, tonic, prost, serde, serde_yaml
- Data Structures: Port Task, NodeConfig, and SchedInfo
- Configuration: Port NodeConfigManager (YAML)
- Algorithms:
	- Port target_node_priority
	- Port best_fit_decreasing
	- Port least_loaded
- Time Logic: Port HyperperiodManager (GCD/LCM logic)

**Acceptance Criteria:**
- `cargo build` succeeds without warnings
- Unit tests match C++ reference outputs for all algorithms

**Sub-Issues:**
1. [Phase 1.1] Initialize Rust Project and Port Core Data Structures
2. [Phase 1.2] Port Scheduling Algorithms and Hyperperiod Logic
3. [Phase 1.3] Unit Testing and Functional Parity Validation

---

## Phase 2: gRPC Connectivity Layer (Piccolo & Node Interfaces)

**Description:**
Transition Timpani-O to a gRPC-first communication model for both Piccolo (upstream) and Timpani-N (downstream).

**Tasks:**

**Upstream (Piccolo):**
- Implement SchedInfoService (gRPC Server)
- Implement FaultServiceClient

**Downstream (Nodes):**
- Define node.proto (NodeScheduler, Orchestrator, and Sync services)
- Implement NodeSchedulerClient with parallel update logic

**Runtime:**
- Configure tokio async runtime for all interfaces

**Success Criteria:**
- Successful end-to-end flow: Piccolo → Timpani-O → Multiple Nodes

**Sub-Issues:**
1. [Phase 2.1] Implement Upstream gRPC Server and Fault Reporting
2. [Phase 2.2] Design and Implement Node Communication Protocol
3. [Phase 2.3] End-to-End Integration Testing with Dummy Services

---

## Phase 1 Sub-Issues

### [Phase 1.1] Initialize Rust Project and Port Core Data Structures
**Tasks:**
- Cargo Setup: Initialize the Rust workspace with dependencies: tokio, tonic, prost, serde, and serde_yaml
- Build System: Create build.rs to compile schedinfo.proto into Rust code
- Core Types: Port Task, NodeConfig, SchedInfo from C++ to Rust structs
- Config Logic: Port NodeConfigManager for YAML parsing

---

### [Phase 1.2] Port Scheduling Algorithms and Hyperperiod Logic
**Tasks:**
- Hyperperiod: Port HyperperiodManager (GCD/LCM logic)
- Core Engine: Port GlobalScheduler skeleton and CPU utilization calculations
- Target Node Priority: Implement the primary scheduling algorithm
- Best Fit Decreasing: Implement the secondary algorithm
- Least Loaded: Implement the fallback algorithm

---

### [Phase 1.3] Unit Testing and Functional Parity Validation
**Tasks:**
- Test Porting: Port all existing C++ unit tests for the three scheduling algorithms to Rust #[test] modules
- Data Generation: Create a script to run both C++ and Rust versions on a sample workload and compare JSON outputs
- Benchmarking: Measure the performance baseline for GlobalScheduler logic

**Success Criteria:**
- 100% parity verified for the standard_workload_set

---

## Phase 2 Sub-Issues

### [Phase 2.1] Implement Upstream gRPC Server and Fault Reporting
**Tasks:**
- SchedInfoService: Implement the gRPC server to handle AddSchedInfo requests
- Message Conversion: Transform incoming Protobuf messages into internal Rust Task and SchedInfo structs
- FaultServiceClient: Implement the client to report deadline misses and scheduling faults back to Piccolo
- Async Runtime: Configure the tokio multi-threaded scheduler to manage these services

---

### [Phase 2.2] Design and Implement Node Communication Protocol
**Tasks:**
- Proto Definition: Design node.proto including NodeSchedulerService, OrchestratorService, and SyncService
- NodeSchedulerClient: Implement the client responsible for distributing schedules to nodes
- Parallel Updates: Use tokio::spawn or FuturesUnordered to push updates to multiple nodes simultaneously
- Sync Logic: Implement multi-node synchronization primitives

---

### [Phase 2.3] End-to-End Integration Testing with Dummy Services
**Tasks:**
- Mock Piccolo: Create a minimal gRPC client to send test workloads to Timpani-O
- Mock Timpani-N: Create a dummy gRPC server that mimics a node receiving schedules
- Flow Validation: Verify the complete data path: Mock Piccolo → Timpani-O (Rust) → Mock Nodes
- Fault Flow: Verify that a simulated node fault is correctly reported back to the Mock Piccolo

**Success Criteria:**
- Successful transmission of a X-node schedule within the target latency

---

# Changelog

- Initial migration plan and epic created
- Phase 1 and Phase 2 breakdown with sub-issues
- Tracking functional parity and gRPC migration milestones

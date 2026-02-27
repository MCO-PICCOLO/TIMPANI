/*
SPDX-FileCopyrightText: Copyright 2026 LG Electronics Inc.
SPDX-License-Identifier: MIT
*/

use std::path::PathBuf;
use std::process;

use clap::Parser;
use tracing::{error, info, warn};

use timpani_o::config::NodeConfigManager;

// ── CLI argument definition ───────────────────────────────────────────────────

/// Timpani-O global scheduler (Rust implementation).
///
/// Example:
///   timpani-o -s 50052 -f localhost -p 50053 -d 50054 \
///             --nodeconfig examples/node_configurations.yaml
#[derive(Debug, Parser)]
#[command(
    name = "timpani-o",
    about = "Timpani-O global scheduler – Rust implementation",
    long_about = None,
)]
struct Cli {
    /// Port for the upstream SchedInfoService gRPC server (receives workloads from Piccolo).
    #[arg(short = 's', long = "sinfoport", default_value_t = 50052)]
    sinfo_port: u16,

    /// FaultService host address (Piccolo gRPC endpoint).
    #[arg(short = 'f', long = "faulthost", default_value = "localhost")]
    fault_host: String,

    /// Port for the FaultService gRPC client (Piccolo endpoint).
    #[arg(short = 'p', long = "faultport", default_value_t = 50053)]
    fault_port: u16,

    /// Port for the downstream node gRPC service (Timpani-N endpoint).
    #[arg(short = 'd', long = "nodeport", default_value_t = 50054)]
    node_port: u16,

    /// Enable the NotifyFault demo (sends one fault notification then clears).
    #[arg(short = 'n', long = "notifyfault", default_value_t = false)]
    notify_fault: bool,

    /// Path to the YAML node configuration file.
    #[arg(short = 'c', long = "nodeconfig")]
    node_config: Option<PathBuf>,
}

// ── Entry point ───────────────────────────────────────────────────────────────

#[tokio::main]
async fn main() {
    // Initialise structured logging.
    // Level is controlled by the RUST_LOG env-var (e.g. RUST_LOG=debug).
    tracing_subscriber::fmt()
        .with_env_filter(
            tracing_subscriber::EnvFilter::try_from_default_env()
                .unwrap_or_else(|_| tracing_subscriber::EnvFilter::new("debug")),
        )
        .init();

    info!("Timpani-O starting up...");

    // ── Parse CLI arguments ───────────────────────────────────────────────────
    let cli = Cli::parse();

    info!(
        sinfo_port   = cli.sinfo_port,
        fault_host   = %cli.fault_host,
        fault_port   = cli.fault_port,
        node_port    = cli.node_port,
        notify_fault = cli.notify_fault,
        node_config  = ?cli.node_config,
        "Configuration"
    );

    // ── Load node configuration ───────────────────────────────────────────────
    let mut node_config_manager = NodeConfigManager::new();

    match &cli.node_config {
        Some(path) => {
            info!("Loading node configuration from: {}", path.display());
            if let Err(e) = node_config_manager.load_from_file(path) {
                error!("Failed to load node configuration: {:#}", e);
                process::exit(1);
            }
        }
        None => {
            warn!("No node configuration file provided, using default node settings");
        }
    }

    // ── Print loaded nodes ────────────────────────────────────────────────────
    if node_config_manager.is_loaded() {
        let nodes = node_config_manager.get_all_nodes();
        info!("Loaded {} node(s):", nodes.len());
        // Sort by name for deterministic output
        let mut sorted: Vec<_> = nodes.values().collect();
        sorted.sort_by_key(|n| &n.name);
        for node in sorted {
            info!(
                "  [{name}]  cpus={cpus:?}  memory={mem}MB  arch={arch}  location={loc}",
                name = node.name,
                cpus = node.available_cpus,
                mem = node.max_memory_mb,
                arch = node.architecture,
                loc = node.location,
            );
        }
    }
}

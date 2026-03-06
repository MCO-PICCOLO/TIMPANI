/*
 * SPDX-FileCopyrightText: Copyright 2026 LG Electronics Inc.
 * SPDX-License-Identifier: MIT
 */

use crate::error::{TimpaniError, TimpaniResult};
use clap::Parser;
use tracing::info;

/// Log level enum matching tt_log_level_t from C
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Default)]
#[repr(u8)]
pub enum LogLevel {
    Silent = 0,
    Error = 1,
    Warning = 2,
    #[default]
    Info = 3,
    Debug = 4,
    Verbose = 5,
}

impl LogLevel {
    /// Parse log level from integer
    pub fn from_u8(level: u8) -> Option<Self> {
        match level {
            0 => Some(LogLevel::Silent),
            1 => Some(LogLevel::Error),
            2 => Some(LogLevel::Warning),
            3 => Some(LogLevel::Info),
            4 => Some(LogLevel::Debug),
            5 => Some(LogLevel::Verbose),
            _ => None,
        }
    }

    /// Convert to tracing filter string
    pub fn to_filter_string(self) -> &'static str {
        match self {
            LogLevel::Silent => "off",
            LogLevel::Error => "error",
            LogLevel::Warning => "warn",
            LogLevel::Info => "info",
            LogLevel::Debug => "debug",
            LogLevel::Verbose => "trace",
        }
    }
}

/// Clock type enum matching clockid_t usage
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum ClockType {
    #[default]
    Realtime,
    Monotonic,
}

/// Configuration structure matching the C context.config
#[derive(Debug, Clone)]
pub struct Config {
    /// CPU affinity for time trigger (-1 for no affinity)
    pub cpu: i32,

    /// RT priority (1-99, -1 for default)
    pub prio: i32,

    /// Port to connect to
    pub port: u16,

    /// Server address
    pub addr: String,

    /// Node ID
    pub node_id: String,

    /// Enable timer synchronization across multiple nodes
    pub enable_sync: bool,

    /// Enable saving plot data file by using BPF
    pub enable_plot: bool,

    /// Enable Apex.OS test mode
    pub enable_apex: bool,

    /// Clock type (CLOCK_REALTIME or CLOCK_MONOTONIC)
    pub clockid: ClockType,

    /// Log level
    pub log_level: LogLevel,
}

impl Default for Config {
    fn default() -> Self {
        Config {
            cpu: -1,
            prio: -1,
            port: 7777,
            addr: "127.0.0.1".to_string(),
            node_id: "1".to_string(),
            enable_sync: false,
            enable_plot: false,
            enable_apex: false,
            clockid: ClockType::Realtime,
            log_level: LogLevel::Info,
        }
    }
}

/// Command-line arguments structure using clap
#[derive(Parser, Debug)]
#[command(name = "timpani-n")]
#[command(about = "Timpani-N node scheduler", long_about = None)]
pub struct CliArgs {
    /// CPU affinity for timetrigger
    #[arg(short = 'c', long, value_name = "CPU_NUM")]
    pub cpu: Option<i32>,

    /// RT priority (1~99) for timetrigger
    #[arg(short = 'P', long, value_name = "PRIO")]
    pub prio: Option<i32>,

    /// Port to connect to
    #[arg(short = 'p', long, value_name = "PORT", default_value = "7777")]
    pub port: u16,

    /// Node ID
    #[arg(short = 'n', long, value_name = "NODE_ID", default_value = "1")]
    pub node_id: String,

    /// Log level (0=silent, 1=error, 2=warning, 3=info, 4=debug, 5=verbose)
    #[arg(short = 'l', long, value_name = "LEVEL", default_value = "3")]
    pub log_level: u8,

    /// Enable timer synchronization across multiple nodes
    #[arg(short = 's', long)]
    pub enable_sync: bool,

    /// Enable saving plot data file by using BPF (<node id>.gpdata)
    #[arg(short = 'g', long)]
    pub enable_plot: bool,

    /// Enable Apex.OS test mode which works without TT schedule info
    #[arg(short = 'a', long)]
    pub enable_apex: bool,

    /// Server host address
    #[arg(value_name = "HOST")]
    pub host: Option<String>,
}

impl Config {
    /// Parse configuration from command-line arguments
    pub fn from_args() -> TimpaniResult<Self> {
        let args = CliArgs::parse();
        Self::from_cli_args(args)
    }

    /// Parse configuration from CliArgs (for testing)
    pub fn from_cli_args(args: CliArgs) -> TimpaniResult<Self> {
        let mut config = Config::default();

        // Parse CPU affinity
        if let Some(cpu) = args.cpu {
            config.cpu = cpu;
        }

        // Parse priority
        if let Some(prio) = args.prio {
            config.prio = prio;
        }

        // Parse port
        config.port = args.port;

        // Parse node ID
        config.node_id = args.node_id;

        // Parse log level
        config.log_level = LogLevel::from_u8(args.log_level).ok_or(TimpaniError::Config)?;

        // Parse boolean flags
        config.enable_sync = args.enable_sync;
        config.enable_plot = args.enable_plot;
        config.enable_apex = args.enable_apex;

        // Parse host address
        if let Some(host) = args.host {
            config.addr = host;
        }

        // Validate the configuration
        config.validate()?;

        Ok(config)
    }

    /// Validate configuration values
    pub fn validate(&self) -> TimpaniResult<()> {
        // Validate priority
        if self.prio < -1 || self.prio > 99 {
            eprintln!(
                "[ERROR] Invalid priority: {} (must be -1 or 1-99)",
                self.prio
            );
            return Err(TimpaniError::Config);
        }

        // Port validation is already handled by u16 type (1-65535)
        if self.port == 0 {
            eprintln!("[ERROR] Invalid port: 0 (must be 1-65535)");
            return Err(TimpaniError::Config);
        }

        // Validate CPU
        if self.cpu < -1 || self.cpu > 1024 {
            eprintln!("[ERROR] Invalid CPU number: {}", self.cpu);
            return Err(TimpaniError::Config);
        }

        // Validate node ID
        if self.node_id.is_empty() {
            eprintln!("[ERROR] Node ID cannot be empty");
            return Err(TimpaniError::Config);
        }

        Ok(())
    }

    /// Log the configuration (matching C implementation's log output)
    pub fn log_config(&self) {
        info!("Configuration:");
        info!("  CPU affinity: {}", self.cpu);
        info!("  Priority: {}", self.prio);
        info!("  Server: {}:{}", self.addr, self.port);
        info!("  Node ID: {}", self.node_id);
        info!("  Log level: {:?}", self.log_level);
        info!(
            "  Sync enabled: {}",
            if self.enable_sync { "yes" } else { "no" }
        );
        info!(
            "  Plot enabled: {}",
            if self.enable_plot { "yes" } else { "no" }
        );
        info!(
            "  Apex.OS test mode: {}",
            if self.enable_apex { "yes" } else { "no" }
        );
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_default_config() {
        let config = Config::default();
        assert_eq!(config.cpu, -1);
        assert_eq!(config.prio, -1);
        assert_eq!(config.port, 7777);
        assert_eq!(config.addr, "127.0.0.1");
        assert_eq!(config.node_id, "1");
        assert!(!config.enable_sync);
        assert!(!config.enable_plot);
        assert!(!config.enable_apex);
    }

    #[test]
    fn test_validate_valid_config() {
        let config = Config::default();
        assert!(config.validate().is_ok());
    }

    #[test]
    fn test_validate_invalid_priority() {
        let mut config = Config::default();
        config.prio = 100;
        assert!(config.validate().is_err());

        config.prio = -2;
        assert!(config.validate().is_err());
    }

    #[test]
    fn test_validate_invalid_cpu() {
        let mut config = Config::default();
        config.cpu = 2000;
        assert!(config.validate().is_err());

        config.cpu = -5;
        assert!(config.validate().is_err());
    }

    #[test]
    fn test_validate_empty_node_id() {
        let mut config = Config::default();
        config.node_id = String::new();
        assert!(config.validate().is_err());
    }

    #[test]
    fn test_validate_invalid_port() {
        let mut config = Config::default();
        config.port = 0;
        assert!(config.validate().is_err());
    }

    #[test]
    fn test_log_level_conversion() {
        assert_eq!(LogLevel::from_u8(0), Some(LogLevel::Silent));
        assert_eq!(LogLevel::from_u8(1), Some(LogLevel::Error));
        assert_eq!(LogLevel::from_u8(2), Some(LogLevel::Warning));
        assert_eq!(LogLevel::from_u8(3), Some(LogLevel::Info));
        assert_eq!(LogLevel::from_u8(4), Some(LogLevel::Debug));
        assert_eq!(LogLevel::from_u8(5), Some(LogLevel::Verbose));
        assert_eq!(LogLevel::from_u8(6), None);
        assert_eq!(LogLevel::from_u8(255), None);
    }

    #[test]
    fn test_log_level_default() {
        let level = LogLevel::default();
        assert_eq!(level, LogLevel::Info);
    }

    #[test]
    fn test_log_level_to_filter_string() {
        assert_eq!(LogLevel::Silent.to_filter_string(), "off");
        assert_eq!(LogLevel::Error.to_filter_string(), "error");
        assert_eq!(LogLevel::Warning.to_filter_string(), "warn");
        assert_eq!(LogLevel::Info.to_filter_string(), "info");
        assert_eq!(LogLevel::Debug.to_filter_string(), "debug");
        assert_eq!(LogLevel::Verbose.to_filter_string(), "trace");
    }

    #[test]
    fn test_clock_type_default() {
        let clock = ClockType::default();
        assert_eq!(clock, ClockType::Realtime);
    }

    #[test]
    fn test_config_with_custom_values() {
        let mut config = Config::default();
        config.cpu = 4;
        config.prio = 50;
        config.port = 8888;
        config.addr = "192.168.1.1".to_string();
        config.node_id = "test-node".to_string();
        config.enable_sync = true;
        config.enable_plot = true;
        config.enable_apex = true;
        config.log_level = LogLevel::Debug;

        assert!(config.validate().is_ok());
        assert_eq!(config.cpu, 4);
        assert_eq!(config.prio, 50);
        assert_eq!(config.port, 8888);
        assert_eq!(config.addr, "192.168.1.1");
        assert_eq!(config.node_id, "test-node");
        assert!(config.enable_sync);
        assert!(config.enable_plot);
        assert!(config.enable_apex);
        assert_eq!(config.log_level, LogLevel::Debug);
    }

    #[test]
    fn test_validate_valid_priority_range() {
        let mut config = Config::default();

        // Test valid priorities
        config.prio = -1;
        assert!(config.validate().is_ok());

        config.prio = 1;
        assert!(config.validate().is_ok());

        config.prio = 50;
        assert!(config.validate().is_ok());

        config.prio = 99;
        assert!(config.validate().is_ok());
    }

    #[test]
    fn test_validate_valid_cpu_range() {
        let mut config = Config::default();

        // Test valid CPU values
        config.cpu = -1;
        assert!(config.validate().is_ok());

        config.cpu = 0;
        assert!(config.validate().is_ok());

        config.cpu = 1024;
        assert!(config.validate().is_ok());
    }

    #[test]
    fn test_log_config() {
        // This test ensures log_config doesn't panic
        let config = Config::default();
        config.log_config();

        let mut config = Config::default();
        config.cpu = 8;
        config.prio = 75;
        config.port = 9999;
        config.addr = "10.0.0.1".to_string();
        config.node_id = "node-test".to_string();
        config.enable_sync = true;
        config.enable_plot = true;
        config.enable_apex = true;
        config.log_level = LogLevel::Verbose;
        config.log_config();
    }

    #[test]
    fn test_log_level_ordering() {
        assert!(LogLevel::Silent < LogLevel::Error);
        assert!(LogLevel::Error < LogLevel::Warning);
        assert!(LogLevel::Warning < LogLevel::Info);
        assert!(LogLevel::Info < LogLevel::Debug);
        assert!(LogLevel::Debug < LogLevel::Verbose);
    }

    #[test]
    fn test_cli_args_parsing() {
        use clap::Parser;

        // Test with default arguments
        let args = CliArgs::try_parse_from(["timpani-n"]).unwrap();
        assert_eq!(args.port, 7777);
        assert_eq!(args.node_id, "1");
        assert_eq!(args.log_level, 3);
        assert!(!args.enable_sync);
        assert!(!args.enable_plot);
        assert!(!args.enable_apex);

        // Test with custom arguments
        let args = CliArgs::try_parse_from([
            "timpani-n",
            "-c",
            "2",
            "-P",
            "50",
            "-p",
            "8888",
            "-n",
            "test-node",
            "-l",
            "4",
            "-s",
            "-g",
            "-a",
            "192.168.1.1",
        ])
        .unwrap();
        assert_eq!(args.cpu, Some(2));
        assert_eq!(args.prio, Some(50));
        assert_eq!(args.port, 8888);
        assert_eq!(args.node_id, "test-node");
        assert_eq!(args.log_level, 4);
        assert!(args.enable_sync);
        assert!(args.enable_plot);
        assert!(args.enable_apex);
        assert_eq!(args.host, Some("192.168.1.1".to_string()));
    }

    #[test]
    fn test_from_cli_args_default() {
        use clap::Parser;

        let args = CliArgs::try_parse_from(["timpani-n"]).unwrap();
        let config = Config::from_cli_args(args).unwrap();

        assert_eq!(config.cpu, -1);
        assert_eq!(config.prio, -1);
        assert_eq!(config.port, 7777);
        assert_eq!(config.node_id, "1");
        assert_eq!(config.log_level, LogLevel::Info);
        assert!(!config.enable_sync);
    }

    #[test]
    fn test_from_cli_args_custom() {
        use clap::Parser;

        let args = CliArgs::try_parse_from([
            "timpani-n",
            "-c",
            "4",
            "-P",
            "80",
            "-p",
            "9999",
            "-n",
            "node-5",
            "-l",
            "5",
            "-s",
            "-g",
            "-a",
            "10.0.0.1",
        ])
        .unwrap();
        let config = Config::from_cli_args(args).unwrap();

        assert_eq!(config.cpu, 4);
        assert_eq!(config.prio, 80);
        assert_eq!(config.port, 9999);
        assert_eq!(config.node_id, "node-5");
        assert_eq!(config.log_level, LogLevel::Verbose);
        assert!(config.enable_sync);
        assert!(config.enable_plot);
        assert!(config.enable_apex);
        assert_eq!(config.addr, "10.0.0.1");
    }

    #[test]
    fn test_from_cli_args_invalid_log_level() {
        use clap::Parser;

        let args = CliArgs::try_parse_from(["timpani-n", "-l", "10"]).unwrap();
        let result = Config::from_cli_args(args);
        assert!(result.is_err());
    }

    #[test]
    fn test_from_cli_args_invalid_priority() {
        use clap::Parser;

        let args = CliArgs::try_parse_from(["timpani-n", "-P", "100"]).unwrap();
        let result = Config::from_cli_args(args);
        assert!(result.is_err());
    }

    #[test]
    fn test_from_cli_args_invalid_cpu() {
        use clap::Parser;

        let args = CliArgs::try_parse_from(["timpani-n", "-c", "2000"]).unwrap();
        let result = Config::from_cli_args(args);
        assert!(result.is_err());
    }

    #[test]
    fn test_cli_long_options() {
        use clap::Parser;

        let args = CliArgs::try_parse_from([
            "timpani-n",
            "--cpu",
            "1",
            "--prio",
            "30",
            "--port",
            "5555",
            "--node-id",
            "long-node",
            "--log-level",
            "2",
            "--enable-sync",
            "--enable-plot",
            "--enable-apex",
        ])
        .unwrap();

        assert_eq!(args.cpu, Some(1));
        assert_eq!(args.prio, Some(30));
        assert_eq!(args.port, 5555);
        assert_eq!(args.node_id, "long-node");
        assert_eq!(args.log_level, 2);
        assert!(args.enable_sync);
        assert!(args.enable_plot);
        assert!(args.enable_apex);
    }

    #[test]
    fn test_validate_all_log_levels() {
        for level_num in 0..=5 {
            let mut config = Config::default();
            config.log_level = LogLevel::from_u8(level_num).unwrap();
            assert!(config.validate().is_ok());
        }
    }

    #[test]
    fn test_config_validation_all_errors() {
        // Test priority too high
        let mut config = Config::default();
        config.prio = 100;
        assert!(matches!(config.validate(), Err(TimpaniError::Config)));

        // Test priority too low
        config = Config::default();
        config.prio = -2;
        assert!(matches!(config.validate(), Err(TimpaniError::Config)));

        // Test CPU too high
        config = Config::default();
        config.cpu = 1025;
        assert!(matches!(config.validate(), Err(TimpaniError::Config)));

        // Test CPU too low
        config = Config::default();
        config.cpu = -2;
        assert!(matches!(config.validate(), Err(TimpaniError::Config)));

        // Test port zero
        config = Config::default();
        config.port = 0;
        assert!(matches!(config.validate(), Err(TimpaniError::Config)));

        // Test empty node_id
        config = Config::default();
        config.node_id = String::new();
        assert!(matches!(config.validate(), Err(TimpaniError::Config)));
    }
}

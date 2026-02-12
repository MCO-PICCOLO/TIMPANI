# Real-time Sample Applications

This project is a sample application for real-time system analysis. It provides periodic execution, deadline monitoring, and runtime statistics collection capabilities.

## Key Features

- **Periodic Execution**: Execute tasks at configurable intervals
- **Deadline Monitoring**: Deadline violation detection and statistics
- **Runtime Measurement**: Accurate execution time measurement and statistics collection
- **Real-time Priority**: Supports SCHED_FIFO scheduling policy
- **Various Workloads**: Supports 8 different workload algorithms
- Newton-Raphson Square Root Calculation (CPU-intensive)
- Fibonacci sequence calculation (sequential calculation)
- Busy wait loop (pure CPU time)
- Matrix multiplication (FPU-intensive)
- Memory-intensive random access
- Cryptographic Hash Simulation
- Mixed workload combinations
- Minor calculations (sequential memory access)
- **Statistical Report**: Minimum/Maximum/Average Execution Time, Deadline Violation Rate, etc.

## Build Method

```bash
git clone https://github.com/MCO-PICCOLO/TIMPANI.git
cd sample-apps
mkdir build
cd build
cmake ..
cmake --build .
```

## How to Use

### Basic Syntax
```bash
sudo ./sample_apps [OPTIONS]
```

### Options

| Option | Description | Default |
|--------|-------------|----------|
| `-p, --period PERIOD` | Period (milliseconds) | 100 |
| `-d, --deadline DEADLINE` | Deadline (milliseconds) | Same as period |
| `-r, --runtime RUNTIME` | Expected runtime (microseconds) | 50000 |
| `-P, --priority PRIORITY` | Real-time priority (1-99) | 50 |
| `-a, --algorithm ALGO` | Algorithm selection (1-8) | 1 |
| `-l, --loops LOOPS` | Loop count/parameter | 10 |
| `-s, --stats` | Enable detailed statistics | Enabled by default |
| `-t, --timer` | Timer-based periodic execution | Signal-based |
| `-h, --help` | Display help | - |

### Example of Execution

#### 1. Basic Execution
```bash
# Run with a 100ms cycle, 50ms deadline, and priority 50
sudo ./sample_apps mytask
```

## Workload Algorithms

### Available Algorithms (8)

| Algorithm | Number | Description | Features | Suitable Uses |
|-----------|--------|-------------|----------|---------------|
| NSQRT | 1 | Newton-Raphson Square Root | CPU-intensive, Floating-point Operations | Numerical Calculation, Basic Performance Testing |
| Fibonacci | 2 | Fibonacci sequence calculation | Sequential computation, integer operations | Algorithm performance testing |
| Busy loop | 3 | Busy waiting loop | Pure CPU time consumption | Precise time control |
| Matrix | 4 | Matrix Multiplication | FPU-intensive, cache-effective | Numerical computation, memory hierarchy testing |
| Memory | 5 | Memory-intensive access | Random memory access, cache misses | Memory system performance testing |
| Crypto | 6 | Cryptographic Hash Simulation | Bitwise Operations, Integer Operations | Security Operation Simulation |
| Mixed | 7 | Mixed Workloads | Diverse Computational Combinations | Real Application Simulation |
| Prime | 8 | Prime Number Calculation | Memory Sequential Access, Conditional Statements | Memory Bandwidth Test |

### Workload-Specific Execution Examples

#### 1. Basic Execution (Newton-Raphson)
```bash
# 100ms cycle, 90ms deadline, default algorithm
sudo ./sample_apps -p 100 -d 90 -a 1 -l 5 basic_task
```

#### 2. Matrix Multiplication Workload
```bash
# 200ms cycle, matrix multiplication, scaling parameter 10
sudo ./sample_apps -p 200 -d 180 -a 4 -l 10 matrix_task
```

#### 3. Memory-intensive workloads
```bash
# 500ms cycle, 32MB memory test
sudo ./sample_apps -p 500 -d 450 -a 5 -l 32 memory_task
```

#### 4. Cryptographic Workloads
```bash
# 50ms cycle, cryptographic hash, 2000 rounds
sudo ./sample_apps -p 50 -d 45 -a 6 -l 20 crypto_task
```

#### 5. Mixed Workloads
```bash
# 100ms cycle, mixed workload, intensity 8
sudo ./sample_apps -p 100 -d 90 -a 7 -l 8 mixed_task
```

#### 6. Prime Number Calculation Workloads
```bash
# Prime number calculation up to 500,000 with a 300ms cycle
sudo ./sample_apps -p 300 -d 270 -a 8 -l 50 prime_task
```

#### 7. Timer-Based Periodic Execution
```bash
# Automated Periodic Execution Using a Timer (No External Signal Required)
sudo ./sample_apps -t -p 100 timer_task
```

## Execution Mode

1. **Signal-Based Mode (Default)**
   - The task will only execute when a SIGRTMIN+2 signal is sent from outside.
   - It is useful when integrating with external schedulers or trigger systems.

```bash
# Terminal 1: Application Execution
sudo ./sample_apps -p 50 -d 45 signal_task

# Terminal 2: Periodic Signal Transmission
while true; do
    killall -s SIGRTMIN+2 sample_apps
    sleep 0.05 # 50ms interval
done
```

2. **Timer-Based Mode**
   - Automatically runs periodically using the internal timer
   - Suitable for independent real-time tasks

```bash
sudo ./sample_apps -t -p 50 -d 45 timer_task
```

## Statistical Information

The application provides the following runtime statistics:

- **Minimum/Maximum/Average Execution Time**: in microseconds
- **Number and percentage of deadline violations**: Percentage relative to total executions
- **Total Execution Count**: Number of completed cycles
- **Last Execution Time**: The time taken for the most recent execution

### Output Example
```text
=== Runtime Statistics for mytask ===
Iterations: 1000
Minimum runtime: 45,230 microseconds
Max runtime: 52,100 microseconds
Average runtime: 48,765 microseconds
Last runtime: 49,120 microseconds
Deadline misses: 12 (1.20%)
Period: 50 ms
Deadline: 45 milliseconds
Expected runtime: 50,000 microseconds
=====================================
```

## Container Execution

### Docker Build
```bash
# Building Ubuntu-based Images
docker build -t ubuntu_latest:sample_apps_v3 -f ./Dockerfile.ubuntu ./

# CentOS-based Image Build
docker build -t centos_latest:sample_apps_v3 -f ./Dockerfile.centos ./
```

### Running Docker
```bash
# Basic Workload Execution (Managing Scheduling Parameters on piccolo and timpani-o/timpani-n)
docker run -it --rm -d \
  --ulimit rtprio=99 \
  --cap-add=sys_nice \
  --privileged \
  --name basic_task \
  ubuntu_latest:sample_apps_v3 \
  sample_apps basic_task

# Execute a specific algorithm workload (specify only the algorithm and loop parameters)
docker run -it --rm -d \
  --ulimit rtprio=99 \
  --cap-add=sys_nice \
  --privileged \
  --name matrix_task \
  ubuntu_latest:sample_apps_v3 \
  sample_apps -a 4 -l 10 matrix_task
```

## Real-Time Performance Tuning

### 1. System Settings
```bash
# Set the CPU governor to performance
echo performance | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor

# Disable IRQ Balancing
sudo systemctl stop irqbalance

# Isolating Specific CPU Cores (Kernel Parameters at Boot)
# isolcpus=2,3 nohz_full=2,3 rcu_nocbs=2,3
```

### 2. Priority Setting Guide
- **1-33**: Low priority (general real-time tasks)
- **34-66**: Medium priority (important real-time tasks)
- **67-99**: High priority (Critical system tasks)

### 3. Recommended Settings for Periods and Deadlines
- **Deadline < Period**: General real-time systems
- **Deadline = Period**: Default setting for periodic tasks
- **Short cycle <( 10ms)**: High-frequency control system
- **Long cycle (100ms)**: Monitoring and logging tasks

## Workload-Based Runtime Measurement

For detailed workload analysis and runtime measurement methods, refer to [`WORKLOAD_GUIDE.md`](./WORKLOAD_GUIDE.md).

### Quick Measurement Guide

1. **Benchmark Measurement**: Measure the baseline execution time for each workload using minimal parameters.
2. **Gradual Increase**: Adjust parameters until the target runtime is reached.
3. **Period Setting**: `Period = Target_Runtime + Safety_Margin + System_Overhead`
4. **Deadline Setting**: `Deadline = Period × (0.8 ~ 0.95)` (depending on strictness)

### Estimated Runtime Reference Table by Workload

| Algorithm | Parameter Example | Expected Runtime | Recommended Period/Deadline |
|----------|---------------|---------------|---------------------|
| NSQRT | `-l 5` | ~100μs | 50ms/45ms |
| Matrix | `-l 5` | ~1.5ms | 100ms/90ms |
| Memory | `-l 8` | ~52ms | 200ms/180ms |
| Crypto | `-l 5` | ~185μs | 50ms/45ms |
| Mixed | `-l 3` | ~5.7ms | 100ms/90ms |
| Prime | `-l 10` | ~ms | 100ms/90ms |

## Troubleshooting

### Permission Issues
```bash
# sudo privileges required for real-time priority configuration
sudo ./sample_apps ...

# or grant users real-time priority privileges
echo "username - rtprio 99" | sudo tee -a /etc/security/limits.conf
```

### Performance Issues
- If CPU usage approaches 100%, increase the cycle interval or reduce the loop count.
- If deadlines are frequently missed, extend the deadlines or reduce the workload.
- Stop unnecessary services to reduce system noise.

### Signal Issues
- If no signal is delivered in signal-based mode, check the process name and PID.
- If multiple instances are running, send a signal to a specific PID.

## License

This project is part of LG Electronics' Timpani project.
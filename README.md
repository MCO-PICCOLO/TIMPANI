# Time Trigger



## Getting started

![TT_1](tt_1.PNG)
![TT_2](tt_2.PNG)
![TT_3](tt_3.PNG)

## Prerequisites

libelf-dev and zlib1g-dev required for libbpf submodule
> NOTE: libbpf has been integrated as a git submodule since [CO-RE feature merge](http://mod.lge.com/hub/timpani/time-trigger/-/commit/c4cddcdbfb337c60579014c9f88f56055edf2b38)
```
sudo apt install -y libelf-dev zlib1g-dev
```

clang and linux-tools(bpftool) required for bpf feature
```
sudo apt install -y clang
sudo apt install -y linux-tools-$(uname -r)
```

pkg-config and libsystemd-dev required for libtrpc submodule
```
sudo apt install -y pkg-config
sudo apt install -y libsystemd-dev
```

libyaml required for dummy_server program

```
sudo apt install -y libyaml-dev
```

## Build

```
git clone http://mod.lge.com/hub/timpani/time-trigger.git
cd time-trigger
git submodule init
git submodule update
mkdir build
cd build
cmake ..
make
```
### Build options

- CONFIG_TRACE_EVENT (ON by default)

  - Gets ftrace dump for sched, timer, signal, and trace_marker events

- CONFIG_TRACE_BPF (ON by default)

  - Activates a bpf program to trace sigwait system call entry/exit of time-triggered tasks
  - Makes it possible to detect deadline misses

- CONFIG_TRACE_BPF_EVENT (OFF by default)

  - Loads a bpf program to keep track of sched_switch and sched_waking events of time-triggered tasks
  - Calculates on-cpu time and scheduling latency

## How to use

execute sample wakee1 process in terminal 1
```
cd build
sudo ./exprocs wakee1 10000
```

execute sample wakee2 process in terminal 2
```
cd build
sudo ./exprocs wakee2 50000
```

execute sample wakee3 process in terminal 3
```
cd build
sudo ./exprocs wakee3 20000
```

execute dummy server, and modify schedinfo.yaml before running if task info is different 
```
cd build
./dummy_server
```

execute time trigger in other terminal
```
cd build
sudo ./timetrigger
```

***

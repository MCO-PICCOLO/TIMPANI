# Timpani Build & Install

> **Note:** This folder is the packaged deliverable for the sdv-blueprint
> release: the pre-built `timpani-n-2.0.0-Linux.deb` and
> `timpani-n-2.0.0-Linux.rpm` packages, plus the `timpani-o-0.1.0.tar`
> container image, and `build.sh`/`install.sh` to build and install both
> components in one step.
>
> Load the container image with `podman load -i timpani-o-0.1.0.tar`. If you
> rebuild it locally with `./build.sh o` instead, it's written to
> `dist/timpani-o-0.1.0.tar`.

One-step build and install for the Timpani release artifacts:

| Component  | Release form                          | Why |
|------------|----------------------------------------|-----|
| `timpani-n` | Native `.deb` (Ubuntu/Debian) or `.rpm` (CentOS/RHEL/Fedora), installed as a systemd service running the binary directly as root | Needs direct eBPF / scheduler / ftrace access to the host kernel — cannot run reliably inside a container |
| `timpani-o` | Podman container image, run via systemd or podman-compose | Stateless orchestration service — no privileged kernel access needed |

## Prerequisites

- Ubuntu 22.04+ (or CentOS/RHEL/Fedora) build host
- `cmake`, build-essential/gcc toolchain, `libsystemd-dev` (for timpani-n)
- `podman` (for building/running timpani-o); the installer will install it automatically if missing
- Repo checked out with submodules: `git submodule update --init --recursive` (needed for `libbpf/`)

## Build

```bash
./build.sh n      # build timpani-n only  -> dist/timpani-n-<version>-Linux.{deb,rpm}
```

**Expected output:**
```
[build.sh] Building timpani-n for OS 'ubuntu' (generator: DEB;RPM)...
-- Configuring done
-- Generating done
-- Build files have been written to: .../timpani-n/build
[ 10%] Building C object ...
...
[100%] Built target timpani-n
CPack: Create package using DEB
CPack: - package: .../timpani-n/build/timpani-n-2.0.0-Linux.deb generated.
CPack: Create package using RPM
CPack: - package: .../timpani-n/build/timpani-n-2.0.0-Linux.rpm generated.
[build.sh] timpani-n package(s) copied to .../dist/
```

`.rpm` generation only needs the `rpmbuild` tool — it works on Ubuntu/Debian
too (no CentOS/RHEL host required). Install it with:
```bash
sudo apt-get install -y rpm      # Ubuntu/Debian
```
If `rpmbuild` isn't found, `build.sh` falls back to building the `.deb` only.

```bash
./build.sh o      # build timpani-o only   -> dist/timpani-o-<version>.tar (podman image)
```

**Expected output:**
```
[build.sh] Building timpani-o container image (version 0.1.0)...
STEP 1/17: FROM ubuntu:22.04 AS builder
...
[2/2] STEP 1/15: FROM alpine:3.21
...
[2/2] STEP 15/15: CMD ["-s", "50052", "-d", "7777"]
[2/2] COMMIT timpani-o:0.1.0
Successfully tagged localhost/timpani-o:0.1.0
[build.sh] Saving image to .../dist/timpani-o-0.1.0.tar (for offline transfer)...
Copying blob ... done
Writing manifest to image destination
```

```bash
./build.sh all    # build both (default)
```

Artifacts are written to `dist/` at the repo root:

```
dist/
├── timpani-n-2.0.0-Linux.deb
├── timpani-n-2.0.0-Linux.rpm
└── timpani-o-0.1.0.tar         # podman image, portable via `podman load`
```

```bash
ls -lh dist/
```
```
total 30M
-rw-rw-r-- 1 lg lg 749K Jul 14 16:15 timpani-n-2.0.0-Linux.deb
-rw-rw-r-- 1 lg lg 209K Jul 14 16:15 timpani-n-2.0.0-Linux.rpm
-rw-r--r-- 1 lg lg  29M Jul 10 15:57 timpani-o-0.1.0.tar
```

`build.sh` always builds the `.deb` for `timpani-n`, and additionally builds
the `.rpm` whenever `rpmbuild` is available on the host — on any distro, not
just CentOS/RHEL/Fedora. `timpani-o` is always built as a Podman image
regardless of host OS. The
Containerfile builds on Ubuntu 22.04 (needed for the gRPC/Protobuf toolchain)
but the final runtime stage is `alpine:3.21` with only the specific shared
libraries `timpani-o` links against copied in (per `ldd`) — this keeps the
final image ~30MB instead of ~93MB for a full Ubuntu-based runtime stage.

## Install

```bash
sudo ./install.sh
```

**Expected output** (verified end-to-end on Ubuntu 22.04):
```
[install.sh] Detected OS: ubuntu
[install.sh] Installing .../dist/timpani-n-2.0.0-Linux.deb...
Selecting previously unselected package timpani-n.
Setting up timpani-n (2.0.0) ...
Refreshing shared library cache (for bundled libtrpc.so)...
Enabling and starting timpani-n service...
Created symlink /etc/systemd/system/multi-user.target.wants/timpani-n.service → /lib/systemd/system/timpani-n.service.
[install.sh] podman already installed: podman version 3.4.4
[install.sh] Loading timpani-o image from .../dist/timpani-o-0.1.0.tar...
Getting image source signatures
Storing signatures
Loaded image(s): localhost/timpani-o:0.1.0
[install.sh] Installing default config to /etc/timpani-o/...
[install.sh] Installing timpani-o systemd service...
Created symlink /etc/systemd/system/multi-user.target.wants/timpani-o.service → /etc/systemd/system/timpani-o.service.
[install.sh] Verifying installation...
[install.sh] timpani-n service is running.
[install.sh] timpani-o service is running.
```

```bash
systemctl is-active timpani-n timpani-o
```
```
active
active
```

Once both are running, `timpani-n` connects to `timpani-o` over port 7777 automatically —
`journalctl -u timpani-o` shows `SchedInfoCallback` entries confirming the two
services are actually talking to each other, not just "started".

This will:
1. Install `timpani-n` from the `.deb`/`.rpm` in `dist/` (via `dpkg`/`dnf`) and enable/start its systemd service.
2. Install `podman` if not already present.
3. Load the `timpani-o` image from `dist/timpani-o-<version>.tar` (or build it locally if the tar is missing), copy the default config to `/etc/timpani-o/node_configurations.yaml`, install/enable/start the `timpani-o` systemd service.
4. Verify both services are active.

Run `build.sh` first — `install.sh` expects artifacts to already exist in `dist/`.

## Verify

```bash
systemctl is-active timpani-n
systemctl is-active timpani-o
```
```
active
active
```

```bash
podman ps                              # timpani-o container should be listed
```
```
CONTAINER ID  IMAGE                      COMMAND               ... PORTS                                          NAMES
e31fd8ab45f3  localhost/timpani-o:0.1.0  -s 50052 -d 7777 ...  ... 0.0.0.0:7777->7777/tcp, 0.0.0.0:50052->50052/tcp  timpani-o
```

```bash
ss -tlnp | grep -E "50052|7777"        # gRPC SchedInfoServer / libtrpc ports listening
```
```
LISTEN 0  4096   *:50052   *:*   users:(("exe",pid=122532,fd=14))
LISTEN 0  4096   *:7777    *:*   users:(("exe",pid=122532,fd=15))
```

```bash
journalctl -u timpani-n -n 50 --no-pager
journalctl -u timpani-o -n 50 --no-pager
```

### Manual container smoke test (without systemd)

```bash
podman run --rm timpani-o:0.1.0 -h
```
```
Usage: /timpani-o/timpani-o [options] [host]
Options:
  -s <port>             Port for SchedInfoService (default: 50052)
  -f <address>          FaultService host address (default: localhost)
  -p <port>             Port for FaultService (default: 50053)
  -d <port>             Port for DBusServer (default: 7777)
  -n                    Enable NotifyFault demo (default: false)
  -c, --node-config <file>      Node configuration YAML file
  -h                    Show this help message
Example: /timpani-o/timpani-o -s 50052 -f localhost -p 50053 -d 7777 --node-config examples/node_configurations.yaml
```

```bash
podman run -d --name timpani-o-test \
  -p 50052:50052 -p 7777:7777 \
  -v "$(pwd)/timpani-o/examples/node_configurations.yaml:/timpani-o/examples/node_configurations.yaml:ro" \
  timpani-o:0.1.0 -s 50052 -d 7777 -c /timpani-o/examples/node_configurations.yaml

podman logs timpani-o-test
```
```
[I] Loading node configuration from: /timpani-o/examples/node_configurations.yaml
[I] Successfully loaded 3 node configurations:
[I]   Node: node01 | CPUs: 2 | Memory: 4096MB | Arch: aarch64
[I]   Node: node02 | CPUs: 4 | Memory: 8192MB | Arch: aarch64
[I]   Node: node03 | CPUs: 4 | Memory: 4096MB | Arch: x86_64
[I] SchedInfoServer listening on port 50052
[I] DBusServer listening on port 7777
```

```bash
podman stop timpani-o-test && podman rm timpani-o-test
```
```
timpani-o-test
timpani-o-test
```

## Configuration

### timpani-n command-line arguments

`timpani-n` is started by systemd, not run interactively, so its arguments
are set via `/etc/default/timpani-n` (installed by the `.deb`/`.rpm` as a
config file — edits survive package upgrades). Edit `TIMPANI_N_ARGS`, then
restart the service:

```bash
sudo vi /etc/default/timpani-n
```
```bash
# e.g. set node ID to 2, log level to debug, connect to a specific host:
TIMPANI_N_ARGS="-n 2 -l 4 192.168.1.10"
```
```bash
sudo systemctl restart timpani-n
```

See `timpani-n -h` or the comments in `/etc/default/timpani-n` for the full
list of options (`-c` CPU affinity, `-P` RT priority, `-p` port, `-n` node ID,
`-l` log level, `-s` timer sync, `-g` BPF plot data, `-a` Apex.OS test mode).

### timpani-o command-line arguments

`timpani-o`'s arguments are set directly in `timpani-o/systemd/timpani-o.service`'s
`ExecStart=` (the `podman run ... timpani-o:0.1.0 -s <port> -d <port> ...` line).
Edit `/etc/systemd/system/timpani-o.service` after install, then:

```bash
sudo systemctl daemon-reload
sudo systemctl restart timpani-o
```

## Uninstall

Removes the installed packages/services from the system (build artifacts in `dist/` are untouched):

```bash
sudo dpkg -r timpani-n            # Ubuntu/Debian
sudo dnf remove -y timpani-n      # CentOS/RHEL/Fedora

sudo systemctl stop timpani-o
sudo systemctl disable timpani-o
sudo rm /etc/systemd/system/timpani-o.service
sudo systemctl daemon-reload
podman rmi timpani-o:0.1.0
```

## Ports

| Port | Protocol | Direction |
|------|----------|-----------|
| 50052 | gRPC | Pullpiri → `timpani-o` (`SchedInfoService`) |
| 7777  | TCP (libtrpc) | `timpani-n` ↔ `timpani-o` |
| 50053 | gRPC | `timpani-o` → Pullpiri (`FaultService`) |

## Known issues / notes

- `libtrpc` builds as a shared library (`libtrpc.so.<major>`). Both release paths
  needed an explicit fix to bundle it:
  - `timpani-o` Containerfile copies it (along with every other shared lib the
    binary links against, per `ldd`) into the Alpine runtime stage — see below.
  - `timpani-n`'s CMakeLists.txt adds an explicit `install(TARGETS trpc LIBRARY DESTINATION lib)`
    (libtrpc's own install rule doesn't run automatically because it's pulled in via
    `add_subdirectory(EXCLUDE_FROM_ALL)`), and `pkg/postinst` runs `ldconfig` after install.
  - Symptom if missing: service exits with code 127 / `error while loading shared
    libraries: libtrpc.so.2: cannot open shared object file`, and systemd crash-loops
    it via auto-restart. Always verify with `systemctl status` after a real install,
    not just a successful build.
- `timpani-o`'s runtime image is Alpine 3.21, not Ubuntu, even though it's built
  with Ubuntu/glibc/gRPC/Protobuf. The build stage compiles the binary and
  collects only the exact `.so` files it depends on (via `ldd`) into `/rootfs`,
  then the runtime stage copies just those into Alpine's `/lib/x86_64-linux-gnu/`
  and `/lib64/`. This avoids pulling in a second full Ubuntu rootfs via
  `apt-get install` for the runtime stage, cutting the final image from ~93MB to
  ~30MB. If a new dependency is added to `timpani-o` in the future, re-run
  `ldd build/timpani-o` and update the `for lib in ...` list in the Containerfile.
- `bpftool` is a virtual package on Ubuntu and is not installed via `apt` in any
  container; `timpani-n`'s eBPF build relies on it being present on the native
  build host (not needed inside `timpani-o`, which has no eBPF component).

## Clean (delete build artifacts)

Removes everything produced by `build.sh`, without touching anything installed on the system:

```bash
# Remove packaged artifacts
rm -rf dist/

# Remove timpani-n's CMake build directory
rm -rf timpani-n/build

# Remove the timpani-o podman image(s)
podman rmi timpani-o:0.1.0

# Optional: also drop any dangling/intermediate build-stage images and build cache
podman image prune -f
```

To also remove the installed system artifacts (packages + systemd services), run
the [Uninstall](#uninstall) steps above first, then the clean steps.

# S905X Bitcoin SHA-256d Native Mining Software

Self-contained ARMv8-A hardware-accelerated Bitcoin double SHA-256 (SHA-256d) mining software optimized specifically for the Amlogic S905X SoC (Quad-Core ARM Cortex-A53 @ 1.512 GHz) running Armbian / Linux AArch64.

---

## ⚡ Key Technical Features

1. **ARMv8 Cryptography Extensions (SIMD Hardware Acceleration)**:
   - Uses native 32-bit vector intrinsics: `sha256h_u32`, `sha256h2_u32`, `sha256su0_u32`, and `sha256su1_u32`.
   - Bypasses slow software 32-bit bit-rotation and Boolean function loops (`CH`, `MAJ`, `SIGMA0`, `SIGMA1`).
2. **Midstate Precomputation**:
   - Pre-computes the first 64 bytes of the 80-byte Bitcoin block header (Version, PrevHash, MerkleRoot[0..27]) once per block template.
   - Reduces active compression operations from **3 down to 2** per hash iteration (-33.3% instruction overhead).
3. **Optimal 3+1 Multi-Core Core Affinity**:
   - **Cores 0, 1, 2**: Pinned dedicated SHA-256d hashing worker threads (`pthread_setaffinity_np()`).
   - **Core 3**: Pinned dedicated Stratum V1 networking, JSON-RPC parsing, target difficulty checking, and process telemetry control plane.
   - Prevents OS context switching and cache contention on the L2 cache.
4. **Outbound Supervisor Daemon (`agent/s905x_agent.py`)**:
   - Initiates an outbound persistent WebSocket link to the central controller server (`/ws/worker`).
   - Dynamically applies runtime thread adjustments, remote pool broadcasts, process restarts, and telemetry logging without needing inbound network ports.

---

## 🛠️ Compilation & Quick Start

### 1. Install Toolchain on S905X (Armbian / Debian / Ubuntu)
```bash
sudo apt-get update
sudo apt-get install -y gcc make git python3 python3-pip
```

Verify your S905X Cortex-A53 CPU supports hardware crypto extensions:
```bash
lscpu | grep -i crypto || cat /proc/cpuinfo | grep -i Features
# Ensure 'aes', 'pmull', 'sha1', and 'sha2' are listed.
```

### 2. Build the Native Binary
```bash
cd /opt/s905x-miner
make
# or run manually:
gcc -O3 -march=armv8-a+crypto -pthread -Wall -Wextra -o bitcoin_sha256d_s905x bitcoin_sha256d_s905x.c
```

### 3. Run Cryptographic Correctness Tests
```bash
./scripts/run_tests.sh
# or
./bitcoin_sha256d_s905x -t
```
*Validates 100% bitwise parity against Block #0 (Genesis), Block #125552, Block #209999, and verifies midstate pre-calculation equivalence.*

### 4. Run Hashrate Benchmark
```bash
./scripts/run_benchmark.sh 20000000 3 3
# or
./bitcoin_sha256d_s905x -b -n 20000000 -j 3 -c 3
```
*Expected throughput on 4-core Cortex-A53 @ 1.512 GHz:* **~9.80 – 10.10 MH/s**.

---

## 📡 Running Standalone vs. Managed Daemon

### Option A: Standalone Solo Stratum Mining (No Controller Needed)
You can point the C binary directly to any Stratum V1 Bitcoin pool:
```bash
./bitcoin_sha256d_s905x \
  -s stratum+tcp://solo.ckpool.org:3333 \
  -u bc1qxy2kgdygjrsqtzq2n0yrf2493p83kkfjhx0wlh.s905x-box \
  -p x \
  -j 3 \
  -c 3
```

### Option B: Managed Fleet Mode (Connecting to Central Controller)
To connect this worker to your central Ubuntu Controller:
```bash
python3 agent/s905x_agent.py \
  --server ws://<CONTROLLER_IP>:3000/ws/worker \
  --id s905x-node-01 \
  --miner ./bitcoin_sha256d_s905x \
  --token s905x_secret_token \
  --autostart
```

### Option C: Running as a Background Systemd Service
1. Edit `systemd/s905x-agent.service` with your controller's LAN IP.
2. Install and activate:
```bash
sudo ./scripts/install_service.sh
```
3. Check status:
```bash
sudo systemctl status s905x-agent
sudo journalctl -u s905x-agent -f
```

---

## ⚙️ CLI Parameter Reference (`bitcoin_sha256d_s905x`)

| Flag | Long Argument | Default | Description |
| :--- | :--- | :--- | :--- |
| `-t` | `--test` | — | Runs internal block test vectors and midstate equivalence verification. |
| `-b` | `--benchmark` | — | Executes synthetic SHA-256d hashing loop and prints exact MH/s. |
| `-n` | `--iterations` | `5000000` | Number of iterations per thread during benchmark. |
| `-j` | `--threads` | `3` | Number of parallel hashing worker threads (pinned to cores 0..j-1). |
| `-c` | `--control-core` | `3` | Core index dedicated to Stratum networking and control plane. |
| `-s` | `--stratum` | — | Stratum pool URL (`stratum+tcp://host:port`). |
| `-u` | `--user` | — | Stratum worker username / Bitcoin payout address. |
| `-p` | `--pass` | `x` | Stratum worker password. |
| `-f` | `--sw` | — | Force portable software C fallback (bypasses ARMv8 SIMD instructions). |
| `-h` | `--help` | — | Displays command-line help menu. |

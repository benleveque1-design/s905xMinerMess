# S905X Bitcoin SHA-256d Mining Fleet & Central Controller

> **High-Performance ARMv8-A Hardware-Accelerated Bitcoin Solo Mining & Centralized Fleet Management for Amlogic S905X (Cortex-A53) Android TV Boxes.**

---

## 📂 Repository Structure (Two-Project Architecture)

The repository is structured into two clearly separated, self-contained sub-projects:

```text
s905xMinerMess/
├── s905x-miner/                        # [PROJECT 1] S905X Worker Hashing Software (Runs on S905X TV Boxes)
│   ├── bitcoin_sha256d_s905x.c         # Native ARMv8 SIMD SHA-256d C Mining Engine
│   ├── Makefile                        # Compilation targets (all, crypto, generic, test, benchmark)
│   ├── config.example.json             # Example local daemon configuration
│   ├── agent/
│   │   ├── s905x_agent.py              # Outbound WebSocket Python supervisor daemon
│   │   └── requirements.txt            # Python dependencies (websockets)
│   ├── scripts/
│   │   ├── build.sh                    # Automated native compilation script
│   │   ├── run_tests.sh                # Correctness test runner (-t)
│   │   ├── run_benchmark.sh            # Benchmark runner (-b)
│   │   └── install_service.sh          # Systemd service installer
│   ├── systemd/
│   │   └── s905x-agent.service         # Systemd service unit for Armbian Linux
│   └── README.md                       # Comprehensive S905X worker documentation
│
├── webapp/                             # [PROJECT 2] Central Web Application & Controller (Ubuntu Server)
│   ├── src/                            # React dashboard source code
│   ├── server.ts                       # Controller Node.js/Express + WebSocket Daemon
│   ├── package.json                    # Dependencies & build scripts
│   ├── vite.config.ts                  # Vite build configuration
│   ├── tsconfig.json                   # TypeScript compiler options
│   ├── index.html                      # HTML entry point
│   ├── .env.example                    # Environment variables template
│   └── README.md                       # Web application documentation
│
├── reference/                          # [OPTIONAL REFERENCE] Non-Core Firmware
│   └── esp32/
│       ├── esp32_miner_controller.ino  # Optional ESP32 Arduino WebSocket controller sketch
│       └── README.md                   # Reference architecture notes
│
└── README.md                           # Master documentation (this file)
```

---

## 🏗️ System Architecture

```text
                    ┌─────────────────────────────┐
                    │     Ubuntu Controller       │
                    │   Central Web Application   │
                    │   React + Node.js/Express   │
                    │   WebSocket Server (:3010)  │
                    └─────────────┬───────────────┘
                                  │
                  Outbound WS Connection (/ws/worker)
                   (No inbound ports needed on S905X)
                                  │
              ┌───────────────────┼───────────────────┐
              │                   │                   │
              ▼                   ▼                   ▼
      ┌───────────────┐   ┌───────────────┐   ┌───────────────┐
      │  S905X Box 1  │   │  S905X Box 2  │   │  S905X Box 3  │
      │ 3 Hash Cores  │   │ 3 Hash Cores  │   │ 3 Hash Cores  │
      │ 1 Ctrl Core   │   │ 1 Ctrl Core   │   │ 1 Ctrl Core   │
      └───────────────┘   └───────────────┘   └───────────────┘
```

---

## 🔍 Codebase Implementation & Verification Status

| Component | Status | Verification & Functional Detail |
| :--- | :--- | :--- |
| **`s905x-miner/bitcoin_sha256d_s905x.c`** | `IMPLEMENTED` | Native C code utilizing ARMv8 Crypto intrinsics (`sha256h_u32`, `sha256h2_u32`, `sha256su0_u32`, `sha256su1_u32`), midstate precomputation, and automatic **3 worker + 1 control core** allocation. *(Note: requires GCC/clang on an AArch64 host to compile)*. |
| **`s905x-miner/agent/s905x_agent.py`** | `IMPLEMENTED` | Standalone Python 3 supervisor daemon connecting outbound to `/ws/worker`, parsing stdout for MH/s, reading `/sys/class/thermal`, and dispatching restart/thread/pool commands. |
| **Central Controller (`webapp/server.ts`)** | `IMPLEMENTED & VERIFIED` | Node.js Express server + WebSocket daemon handling client browser connections (`/ws/client`) and worker connections (`/ws/worker`) on configurable `PORT` (default `3010`). Tested and compiles cleanly. |
| **Web Dashboard (`webapp/src/`)** | `IMPLEMENTED & VERIFIED` | Full React + Tailwind CSS dashboard with live node metrics, per-worker and fleet-wide controls, thread sliders, and pool broadcast tools. Verified with `npm run build` and `tsc`. |
| **Simulated Workers (`webapp/server.ts`)** | `SIMULATED` | In-memory simulated worker loop used in development/preview when zero physical S905X nodes are connected. Bypassed automatically when real hardware connects. |
| **ESP32 Controller (`reference/esp32/`)** | `OPTIONAL REFERENCE` | Standalone Arduino C++ sketch provided as reference. Not required for the primary Ubuntu architecture. |

---

## 🚀 Quick Deployment Guide

### PROJECT 1: Deploying to S905X Worker Nodes (Armbian Linux AArch64)

#### 1. Install Toolchain on S905X
```bash
sudo apt-get update
sudo apt-get install -y gcc make git python3 python3-pip
```

#### 2. Build the Native C Miner
```bash
cd /opt
git clone https://github.com/benleveque1-design/s905xMinerMess.git
cd s905xMinerMess/s905x-miner

# Build with ARMv8 Crypto acceleration
make
```

#### 3. Run Correctness Tests & Benchmark
```bash
# Verify cryptographic correctness (100% bitwise parity)
./scripts/run_tests.sh

# Run 3+1 core benchmark (Target: ~9.80 - 10.10 MH/s on Cortex-A53 @ 1.512 GHz)
./scripts/run_benchmark.sh 20000000 3 3
```

#### 4. Configure & Start the Outbound Worker Agent
Edit `systemd/s905x-agent.service` with your Ubuntu server IP:
```ini
ExecStart=/usr/bin/python3 /opt/s905xMinerMess/s905x-miner/agent/s905x_agent.py \
  --server ws://192.168.1.100:3010/ws/worker \
  --id s905x-node-01 \
  --miner /opt/s905xMinerMess/s905x-miner/bitcoin_sha256d_s905x \
  --token s905x_secret_token \
  --autostart
```

Install and enable the background systemd daemon:
```bash
sudo ./scripts/install_service.sh
```

---

### PROJECT 2: Deploying Central Web Application (Ubuntu Server)

#### 1. Install Node.js 20 LTS & Build Tools
```bash
sudo apt-get update
sudo apt-get install -y curl git build-essential ufw

curl -fsSL https://deb.nodesource.com/setup_20.x | sudo -E bash -
sudo apt-get install -y nodejs
```

#### 2. Clone Repository & Install Dependencies
```bash
cd /opt
git clone https://github.com/benleveque1-design/s905xMinerMess.git
cd s905xMinerMess/webapp
npm install
```

#### 3. Configure Environment Variables
```bash
cp .env.example .env
```
Default configuration values:
```env
PORT=3010
NODE_ENV=production
WORKER_AUTH_TOKEN=s905x_secret_token
```

#### 4. Build Production Bundle
```bash
npm run build
```
*Compiles the React SPA to `webapp/dist/` and bundles `server.ts` into `webapp/dist/server.cjs` via esbuild.*

#### 5. Open Controller Firewall Port
```bash
sudo ufw allow 3010/tcp
sudo ufw reload
```

#### 6. Setup Systemd Service on Controller
Create `/etc/systemd/system/s905x-controller.service`:
```ini
[Unit]
Description=S905X Central Mining Fleet Controller
After=network.target

[Service]
Type=simple
User=root
WorkingDirectory=/opt/s905xMinerMess/webapp
ExecStart=/usr/bin/node dist/server.cjs
Restart=always
RestartSec=5
Environment=NODE_ENV=production
Environment=PORT=3010

[Install]
WantedBy=multi-user.target
```

Enable and start the controller:
```bash
sudo systemctl daemon-reload
sudo systemctl enable --now s905x-controller
```

---

## 📡 WebSocket Protocol Contract

Communication occurs over JSON WebSocket messages on `/ws/worker`:

### 1. Worker Auth & Registration (Worker $\rightarrow$ Server)
```json
{
  "type": "auth",
  "workerId": "s905x-node-01",
  "token": "s905x_secret_token",
  "name": "Living Room S905X",
  "cores": 4,
  "arch": "aarch64 Cortex-A53",
  "hwCrypto": true,
  "agentVersion": "2.0.0"
}
```

### 2. Live Telemetry Heartbeat (Worker $\rightarrow$ Server, 2s interval)
```json
{
  "type": "telemetry",
  "workerId": "s905x-node-01",
  "name": "Living Room S905X",
  "state": "RUNNING",
  "threads": 3,
  "maxCores": 4,
  "hashrateMhs": 9.87,
  "tempC": 58.4,
  "cpuFreqMhz": 1512,
  "sharesFound": 42,
  "sharesAccepted": 42,
  "sharesRejected": 0,
  "uptime": 7200,
  "pool": {
    "url": "stratum+tcp://solo.ckpool.org:3333",
    "user": "bc1qxy2kgdygjrsqtzq2n0yrf2493p83kkfjhx0wlh.s905x-01",
    "pass": "x"
  }
}
```

### 3. Downstream Commands (Server $\rightarrow$ Worker)
```json
{ "type": "command", "cmdId": "cmd-101", "workerId": "s905x-node-01", "action": "start" }
{ "type": "command", "cmdId": "cmd-102", "workerId": "s905x-node-01", "action": "stop" }
{ "type": "command", "cmdId": "cmd-103", "workerId": "s905x-node-01", "action": "restart" }
{ "type": "command", "cmdId": "cmd-104", "workerId": "s905x-node-01", "action": "set_threads", "params": { "threads": 3 } }
{ "type": "command", "cmdId": "cmd-105", "workerId": "all", "action": "set_pool", "params": { "url": "stratum+tcp://solo.ckpool.org:3333", "user": "bc1q...", "pass": "x" } }
```

---

## 🎯 Verification Checklist

1. **Standalone C Miner Tests**: Run `./s905x-miner/scripts/run_tests.sh` on the S905X. Confirm all 4 tests pass.
2. **Benchmark Verification**: Run `./s905x-miner/scripts/run_benchmark.sh` on the S905X. Confirm hashrate is ~9.87 MH/s across 3 hashing threads.
3. **Web Dashboard Build**: Run `npm run build` on the Ubuntu controller. Confirm zero TypeScript or bundling errors.
4. **Live Worker Appearance**: Open `http://<CONTROLLER_IP>:3010` in your browser. Verify the physical S905X appears with its real IP, temperature, frequency, and `ARMv8 Crypto: Enabled`.

---

## 📄 License
This project is open-source software licensed under the **MIT License**.

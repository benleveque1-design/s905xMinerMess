# S905X Bitcoin SHA-256d Mining Fleet

Native C SHA-256d miner for Amlogic S905X (Cortex-A53, ARMv8-A Crypto Extension)
TV boxes, supervised by a Python WebSocket agent and managed by a central
Node.js controller with a React dashboard.

## Overview

```text
                    ┌─────────────────────────────┐
                    │     Controller (Node.js)    │
                    │   React + Express + WS      │
                    │   Port 3010                 │
                    └─────────────┬───────────────┘
                                  │
                  Outbound WS (/ws/worker)
                                  │
              ┌───────────────────┼───────────────────┐
              │                   │                   │
              ▼                   ▼                   ▼
      ┌───────────────┐   ┌───────────────┐   ┌───────────────┐
      │  .118 forge   │   │  .224 recovery│   │  .208 aml-box │
      │  3 Hash Cores │   │  3 Hash Cores │   │  3 Hash Cores │
      │  1 Ctrl Core  │   │  1 Ctrl Core  │   │  1 Ctrl Core  │
      │  systemd      │   │  systemd      │   │  standalone   │
      └───────────────┘   └───────────────┘   └───────────────┘
```

Workers initiate WebSocket connections outbound; the controller never connects
to workers, so boxes need no inbound firewall rules.

## Components

| Component | Path | Description |
|:---|:---|:---|
| **C Miner** | `s905x-miner/bitcoin_sha256d_s905x.c` | Native SHA-256d engine with ARMv8 Crypto intrinsics |
| **Python Agent** | `s905x-miner/agent/s905x_agent.py` | Supervisor daemon — connects to controller, reports telemetry, executes commands |
| **Controller** | `webapp/server.ts` | Express + WebSocket server handling REST API, dashboard, and worker connections |
| **Dashboard** | `webapp/src/` | React + Tailwind SPA with live metrics and fleet controls |

## Fleet

| Node | IP | Worker ID | Hostname | Status |
|:---|:---|:---|:---|:---|
| Controller | 192.168.1.156 | — | printserver | systemd `s905x-controller` |
| Forge | 192.168.1.118 | s905x-forge-118 | forge-box | systemd `s905x-agent` |
| Recovery | 192.168.1.224 | s905x-recovery-224 | recovery-box | systemd `s905x-agent` |
| AML | 192.168.1.208 | s905x-node-208 | aml-s9xx-box | standalone agent |

All nodes: ARMv8 S905X (Cortex-A53, 4 cores), Armbian, `ben`/`Cheese` credentials.

## Quick Start

### Build the miner

```bash
cd s905x-miner
make
```

### Run tests

```bash
make test          # Correctness suite (6 test groups)
make benchmark     # Performance benchmark
```

### Start the controller

```bash
cd webapp
cp .env.example .env
# Edit .env to set WORKER_AUTH_TOKEN
npm install
npm run build
npm start          # or: node dist/server.cjs
```

Dashboard: http://localhost:3010/

### Deploy to a worker node

```bash
# On the S905X box:
sudo apt-get install -y gcc make git python3-websockets libc6-dev
cd /opt && sudo git clone https://github.com/benleveque1-design/s905xMinerMess.git
cd s905xMinerMess/s905x-miner && make

# Configure agent
sudo tee /etc/default/s905x-agent <<EOF
WORKER_AUTH_TOKEN=<token>
CONTROLLER_WS_URL=ws://192.168.1.156:3010/ws/worker
WORKER_ID=<unique-id>
EOF
sudo chmod 600 /etc/default/s905x-agent

# Install and start
sudo -E ./scripts/install_service.sh
sudo systemctl enable --now s905x-agent
```

## Mining Performance

| Mode | Hashrate | Notes |
|:---|:---|:---|
| Synthetic benchmark | **10.08 MH/s** | `-j 3 -c 3`, 3 hash threads + 1 control core, 1.512 GHz |
| Real-pool sustained | **8.98–8.99 MH/s** | ~32 min authorized test against external Stratum pool |

All measurements on S905X at 1.512 GHz, no frequency throttling.

## Architecture

### Threading (`-j 3 -c 3`)

- **Cores 0–2**: Hash threads (pinned, run SHA-256d computation)
- **Core 3**: Control thread (Stratum session, work distribution, share submission)

The `-j 3 -c 3` configuration is load-bearing. Other combinations may cause
core contention or leave the control thread unpinned.

### Protocol

Communication uses JSON WebSocket messages on `/ws/worker`:

1. **Auth** — worker sends `workerId`, `token`, `cores`, `arch`, `hwCrypto`
2. **Telemetry** — every ~2s: `hashrateMhs`, `tempC`, `cpuFreqMhz`, shares, `pool`
3. **Commands** — controller sends: `start`, `stop`, `restart`, `set_threads`, `set_pool`

### Pool Safety

Default pool is `stratum+tcp://127.0.0.1:9333` (dead endpoint). Miner idles
in retry loop with 0 MH/s. A `/etc/hosts` guardrail pins `solo.ckpool.org`
to localhost. Other pool hostnames resolve normally.

## Thermal

| Metric | Value |
|:---|:---|
| Idle temperature | 42–45 °C |
| Sustained load | 69–70 °C |
| Passive trip | 80 °C |
| Hot / critical | 90 °C / 110 °C |

Agent enforces thermal hard stop at 80 °C — miner is stopped immediately.
Temperature is reported as `null` when sensor is unavailable.

## Development

```bash
# Webapp dev mode (Vite HMR)
cd webapp
npm run dev

# Lint (TypeScript check)
npm run lint

# Build production
npm run build
```

No test framework for the webapp — verification is `tsc --noEmit` + `npm run build`.

## Deployment Notes

- Controller state is **in-memory only** — restart wipes fleet and pool
- Workers auto-reconnect within seconds of controller restart
- Re-apply pool settings after any controller restart
- The `/opt/s905xMinerMess` path is load-bearing for systemd units
- Python agent requires `python3-websockets` (apt package, not pip)

## License

MIT License

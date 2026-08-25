# S905X Bitcoin SHA-256d Mining Fleet & Central Controller

Native C SHA-256d miner for Amlogic S905X (Cortex-A53, ARMv8-A Crypto Extension)
TV boxes, supervised by a Python WebSocket agent and managed by a central
Node.js controller with a React dashboard.

## Overview

The repository contains two independent projects:

- **`s905x-miner/`** — a self-contained worker stack that runs on each S905X
  box: a native mining engine using ARMv8 SHA-2 intrinsics (generic fallback
  keeps it buildable elsewhere), plus a supervisor daemon that connects
  *outbound* to the controller, reports telemetry every ~2 s, and executes
  commands (start/stop/restart/thread count/pool).
- **`webapp/`** — the central controller: one Node.js process serving the REST
  API, the dashboard WebSocket (`/ws/client`), and the worker WebSocket
  (`/ws/worker`) on a single port (default `3010`), plus the React SPA.

Both sides have been verified live on real hardware, including an authorized
real-pool mining test (see [Mining Performance](#mining-performance) and
[Thermal Characterization](#thermal-characterization)).

## Repository Structure

```text
s905xMinerMess/
├── s905x-miner/                        # [PROJECT 1] S905X worker hashing software
│   ├── bitcoin_sha256d_s905x.c         # Native ARMv8 SIMD SHA-256d C mining engine
│   ├── Makefile                        # Targets: all, crypto, generic, test, benchmark
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
│   └── README.md                       # S905X worker documentation
│
├── webapp/                             # [PROJECT 2] Central web application & controller
│   ├── src/                            # React dashboard source code
│   ├── server.ts                       # Controller Node.js/Express + WebSocket daemon
│   ├── package.json                    # Dependencies & build scripts
│   ├── vite.config.ts                  # Vite build configuration
│   ├── tsconfig.json                   # TypeScript compiler options
│   ├── index.html                      # HTML entry point
│   ├── .env.example                    # Environment variables template
│   └── README.md                       # Web application documentation
│
└── README.md                           # Master documentation (this file)
```

## System Architecture

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

Workers always initiate the WebSocket connection outbound; the controller never
connects to workers, so boxes need no inbound firewall rules.

## Implementation & Verification Status

| Component | Status | Verification & Functional Detail |
| :--- | :--- | :--- |
| **`s905x-miner/bitcoin_sha256d_s905x.c`** | `IMPLEMENTED & VERIFIED` | Native C using ARMv8 Crypto intrinsics (`vsha256hq_u32`, `vsha256h2q_u32`, `vsha256su0q_u32`, `vsha256su1q_u32`), midstate precomputation, automatic **3 hash threads + 1 control core** allocation (hash threads pinned to cores 0–2, core 3 reserved for OS/control plane). Depth-aware `mining.subscribe` response parser (`stratum_parse_subscribe`) extracts the pool's extranonce1 (hex-validated) and clamps extranonce2 size to 1..8; malformed responses fall back to defaults. Covered by regression tests. Requires GCC/clang on an AArch64 host. |
| **`s905x-miner/agent/s905x_agent.py`** | `IMPLEMENTED & VERIFIED` | Python 3 supervisor daemon connecting outbound to `/ws/worker`, parsing miner stdout for MH/s, reading `/sys/class/thermal`, dispatching start/stop/restart/thread/pool commands (blocking restarts dispatched off the event loop). Verified live against a real controller and real pool. |
| **Central Controller (`webapp/server.ts`)** | `IMPLEMENTED & VERIFIED` | Express server + WebSocket daemon handling `/ws/client` (dashboard) and `/ws/worker` (workers) on configurable `PORT` (default `3010`). Refuses to start without `WORKER_AUTH_TOKEN`; mutating REST routes require an `x-auth-token` header (timing-safe compare); simulated fleet retires when the first real worker authenticates. Verified end-to-end with a live agent. |
| **Web Dashboard (`webapp/src/`)** | `IMPLEMENTED & VERIFIED` | React + Tailwind dashboard with live node metrics, per-worker and fleet-wide controls, thread sliders, and pool broadcast tools. All worker control traffic flows over the authenticated dashboard WebSocket. Verified with `npm run lint` (`tsc --noEmit`) and `npm run build`. |
| **Simulated Workers (`webapp/server.ts`)** | `SIMULATED` | In-memory simulated worker loop for development/preview when zero physical S905X nodes are connected. Retired automatically when real hardware connects. |

---

## S905X / ARM64 Support

The miner builds a portable AArch64 binary: the ARMv8 Crypto Extension hashing
functions opt in per-function via `target("+crypto")`, and at startup the miner
checks `getauxval(AT_HWCAP) & HWCAP_SHA2` and selects the hardware backend only
when the CPU actually advertises SHA-2. Cores without the extension (e.g. the
original Amlogic S905 / GXBB) automatically run the software fallback instead —
one binary serves both SoC families:

- **S905X-class (GXL)**: crypto extension present → `sha256_hw` backend.
- **S905-class (GXBB)**: no crypto extension → portable C software backend.

The Python agent detects the same capability from `/proc/cpuinfo` (`Features`
line) and reports it truthfully in `auth` (`hwCrypto`), along with the CPU
model string in `arch`. Thermal readings that are physically impossible
(e.g. the GXBB SCPI invalid sentinel `-1000`) are reported as `tempC: null`
rather than fabricated numbers.

The reference hardware is an Amlogic S905X box (Cortex-A53, 4× cores) running
Armbian; the production deployment compiles natively on the box itself. Both SoC
families have been benchmarked side by side on real hardware (`-j 3 -c 3`,
≥30 min per mode): S905X sustains **~9.0 MH/s** mining a live pool / **10.1 MH/s**
synthetic with the crypto backend, while an original S905 (GXBB, software
fallback) sustains **~1.0 MH/s** in both modes — all at flat ceiling clocks with
zero throttling. Methodology, thermal caveats, and full results:
[docs/s905-integration.md](docs/s905-integration.md).

A layered analysis of how far the stack can be stripped toward a minimal
kernel or bare metal — with measured baselines and a built-but-unbooted 10.3 MB
minimal kernel — lives in [docs/s905x-minimal-stack.md](docs/s905x-minimal-stack.md).

## Building

```bash
# On the S905X (or any machine with gcc/make):
cd s905x-miner
make
```

## Testing

```bash
# Correctness suite: 6 test groups covering block-header KATs,
# HW/SW backend agreement, Stratum subscribe parsing regressions,
# and extranonce1 propagation into the coinbase.
./scripts/run_tests.sh

# Benchmark: 1e9 hashes across 3 hash threads + 1 reserved control core.
./scripts/run_benchmark.sh 1000000000 3
```

Both suites pass on aarch64 (ARMv8 Crypto backend engaged) and on x86 fallback.

---

## Production Deployment

### Worker nodes (S905X, Armbian Linux AArch64)

1. Install the toolchain and check out the repository at exactly
   `/opt/s905xMinerMess` — the systemd unit hardcodes absolute paths under it:

   ```bash
   sudo apt-get update
   sudo apt-get install -y gcc make git python3 python3-pip python3-websockets
   cd /opt
   sudo git clone https://github.com/benleveque1-design/s905xMinerMess.git
   ```

2. Build and run the correctness tests:

   ```bash
   cd /opt/s905xMinerMess/s905x-miner
   make
   ./scripts/run_tests.sh
   ```

3. Configure and install the agent service. The agent reads its configuration
   from the environment (no secrets in unit files):

   | Variable | Meaning | Default |
   | :--- | :--- | :--- |
   | `WORKER_AUTH_TOKEN` | Shared token; must match the controller's value | *required* |
   | `CONTROLLER_WS_URL` | Controller WebSocket endpoint | `ws://127.0.0.1:3010/ws/worker` |

   ```bash
   export WORKER_AUTH_TOKEN="<same token as the controller>"
   export CONTROLLER_WS_URL="ws://<controller-ip>:3010/ws/worker"
   sudo -E ./scripts/install_service.sh
   ```

   The installer provisions `/etc/default/s905x-agent` (chmod 600), verifies
   that the Python `websockets` package is present — the agent refuses to start
   without it rather than installing packages at runtime — and enables the unit
   with `--autostart`, so mining begins at boot.

### Controller (Ubuntu Server)

1. Install Node.js 20 LTS and build tools:

   ```bash
   sudo apt-get update
   sudo apt-get install -y curl git build-essential ufw
   curl -fsSL https://deb.nodesource.com/setup_20.x | sudo -E bash -
   sudo apt-get install -y nodejs
   ```

2. Check out the repository under `/opt/s905xMinerMess` and install webapp
   dependencies:

   ```bash
   cd /opt
   sudo git clone https://github.com/benleveque1-design/s905xMinerMess.git
   cd s905xMinerMess/webapp
   npm install
   ```

3. Configure environment:

   ```bash
   cp .env.example .env
   echo "WORKER_AUTH_TOKEN=$(openssl rand -hex 32)" >> .env
   chmod 600 .env
   ```

   Defaults: `PORT=3010`, `NODE_ENV=production`. The controller refuses to start
   when `WORKER_AUTH_TOKEN` is unset. Mutating REST endpoints require an
   `x-auth-token` header; GET endpoints stay open for monitoring.

4. Build the production bundle:

   ```bash
   npm run build
   ```

   This compiles the React SPA to `webapp/dist/` and bundles `server.ts` into
   `webapp/dist/server.cjs`.

5. Create `/etc/systemd/system/s905x-controller.service`:

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

   The controller performs no privileged operations (unprivileged port, reads
   only its checkout), so `User=root` above can be replaced with any
   unprivileged user that owns the checkout.

6. Enable and start:

   ```bash
   sudo ufw allow 3010/tcp   # only if ufw is enforcing
   sudo systemctl daemon-reload
   sudo systemctl enable --now s905x-controller
   ```

7. Open `http://<controller-ip>:3010/` for the dashboard. Three simulated
   workers appear until the first real worker authenticates on `/ws/worker`,
   at which point they retire automatically.

---

## WebSocket Protocol Contract

Communication occurs over JSON WebSocket messages on `/ws/worker`:

### 1. Worker Auth & Registration (Worker → Server)
```json
{
  "type": "auth",
  "workerId": "s905x-node-01",
  "token": "<WORKER_AUTH_TOKEN>",
  "name": "Living Room S905X",
  "cores": 4,
  "arch": "aarch64 Cortex-A53",
  "hwCrypto": true,
  "agentVersion": "2.2.0"
}
```
`arch` is the detected CPU model string and `hwCrypto` reflects the kernel's
advertised ARMv8 SHA-2 support (S905X-class: `true`; S905/GXBB-class: `false`).

### 2. Live Telemetry Heartbeat (Worker → Server, 2 s interval)
`tempC` is a number when the thermal sensor is valid, or `null` when no
trustworthy reading exists (the dashboard renders `N/A`).
```json
{
  "type": "telemetry",
  "workerId": "s905x-node-01",
  "name": "Living Room S905X",
  "state": "RUNNING",
  "threads": 3,
  "maxCores": 4,
  "hashrateMhs": 10.08,
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

### 3. Downstream Commands (Server → Worker)
```json
{ "type": "command", "cmdId": "cmd-101", "workerId": "s905x-node-01", "action": "start" }
{ "type": "command", "cmdId": "cmd-102", "workerId": "s905x-node-01", "action": "stop" }
{ "type": "command", "cmdId": "cmd-103", "workerId": "s905x-node-01", "action": "restart" }
{ "type": "command", "cmdId": "cmd-104", "workerId": "s905x-node-01", "action": "set_threads", "params": { "threads": 3 } }
{ "type": "command", "cmdId": "cmd-105", "workerId": "all", "action": "set_pool", "params": { "url": "stratum+tcp://solo.ckpool.org:3333", "user": "bc1q...", "pass": "x" } }
```

Agents accept pool parameters either flat as shown or wrapped as `params: { "pool": {...} }` (the dashboard uses the wrapped form). When a **dashboard client** sends `set_pool` on `/ws/client`, the controller also merges it into its global default pool and broadcasts `{ "type": "pool_update", "pool": {...} }` — identical semantics to `POST /api/pool`.

---

## Mining Performance

Two distinct kinds of measurement exist; do not conflate them.

**Synthetic benchmark** (`./scripts/run_benchmark.sh`, pure hashing with
locally generated work, no Stratum overhead):

- 1,000,000,000 hashes in 99.18 s = **10.08 MH/s** across 3 hash threads on the
  S905X at 1.512 GHz (cores 0–2 pinned, core 3 reserved for OS/control),
  43→55 °C during the run, no frequency throttling.

**Real-pool sustained mining** (authorized live test against an external
Stratum pool — see [Thermal Characterization](#thermal-characterization)):

- **8.98–8.99 MH/s**, effectively flat for ~32 minutes. The gap to the
  synthetic number is consistent with real-world Stratum job turnaround and
  share submission overhead; no tuning was attempted.

## Thermal Characterization

The authoritative thermal data comes from the same **real-pool test**
(2026-08-21/22, ~32.4 minutes of continuous mining on the production S905X).
This is a measurement under load, not the miner's `-t` correctness suite.

| Metric | Observed |
| :--- | :--- |
| Hashrate | 8.98–8.99 MH/s |
| Sustained clock | 1.512 GHz |
| Test duration | ~32.4 min |
| Idle temperature baseline | 42–45 °C |
| Peak temperature | 70 °C |
| Stable range (final portion) | ~69–70 °C |
| Passive thermal trip (kernel) | 80 °C |
| Hot / critical trips (kernel) | 90 °C / 110 °C |
| Cooling engagement | None (`cur_state=0` throughout) |
| Frequency dips while hashing | None |
| Accepted shares | 2 |
| Rejected shares | 0 |

The two accepted shares are strong end-to-end evidence that the full chain —
pool connection → Stratum subscribe/authorize → job notification → hashing →
share submission → pool acceptance — functioned against a live pool.

**Qualification:** this demonstrates stable thermal equilibrium over
approximately 32 minutes under the tested ambient conditions and configuration.
It does **not** prove the box can mine indefinitely; a multi-hour soak would be
required for a stronger long-duration claim. The current measured sustained-load
baseline is **69–70 °C** (an earlier ~5½-minute test appeared to plateau near
64 °C; that reading was premature).

## Sandbox / Pool Safety

The production deployment intentionally does **not** mine against a real pool
by default. The configured pool is:

```text
stratum+tcp://127.0.0.1:9333
```

a deliberately dead local endpoint. In this sandboxed state it is normal and
expected that:

- the miner stays in its Stratum retry loop without contacting any real pool;
- reported hashrate is legitimately **zero** (no work is received, so nothing
  is hashed);
- CPU utilization is low (~1%) and the heatsink stays cool.

A guardrail line in `/etc/hosts` pins the agent's hardcoded default pool:

```text
127.0.0.1 solo.ckpool.org
```

Note precisely what this does and does not do: it redirects **only the
hostname `solo.ckpool.org`** to localhost. It does **not** block arbitrary
mining pools — a different hostname resolves normally via DNS. (During the
authorized real-pool test above, `pool.basedmining.xyz` resolved and connected
without touching the guardrail.)

To authorize real mining: change the pool through the dashboard's pool editor
(WebSocket path) or `POST /api/pool` with the token, then remove the
`/etc/hosts` line only if the target hostname is `solo.ckpool.org`. After any
controller restart the in-memory pool resets to the default — re-apply the
sandbox (or your chosen pool) immediately.

---

## Verification Checklist & Measured Results

1. **Standalone C miner tests**: `./s905x-miner/scripts/run_tests.sh` on the
   S905X — all 6 test groups pass (block header KATs, HW/SW backend agreement,
   Stratum subscribe parsing regression, extranonce1 propagation into the
   coinbase). Measured: PASS on S905X (aarch64, ARMv8 Crypto) and on x86 fallback.
2. **Benchmark**: `./s905x-miner/scripts/run_benchmark.sh 1000000000 3` —
   measured 10.08 MH/s (see [Mining Performance](#mining-performance)).
3. **Web dashboard build**: `npm run build` — zero TypeScript or bundling errors.
4. **Live worker appearance**: open `http://<controller-ip>:3010/` — the
   physical S905X appears with real IP, temperature, frequency, and
   `ARMv8 Crypto: Enabled`.
5. **Command & telemetry path**: verified live end-to-end — auth (bad token →
   close 4001), ~2 s telemetry ingestion, start/stop/restart/set_threads/
   rename/set_pool with acks relayed to dashboards, OFFLINE detection after
   >8 s silence.
6. **Authorized real-pool test**: performed against an external Stratum pool
   with explicit owner authorization; results in
   [Thermal Characterization](#thermal-characterization). Sandbox fully
   restored afterward.

---

## Troubleshooting

| Symptom | Likely cause / fix |
| :--- | :--- |
| Dashboard shows 0 MH/s and the box is cool | Expected while sandboxed — see [Sandbox / Pool Safety](#sandbox--pool-safety). Not a fault. |
| Worker shows `OFFLINE` | Controller marks workers offline after >8 s without telemetry. Check the agent service (`systemctl status s905x-agent`) and controller reachability. |
| Agent service runs but journal is empty | Python stdout is block-buffered under systemd. Add `PYTHONUNBUFFERED=1` to the unit if logs are wanted. |
| Controller exits immediately at startup | `WORKER_AUTH_TOKEN` is unset — create `webapp/.env` from `.env.example`. |
| Dashboard unreachable after network changes | The server LAN IP is DHCP; if it changed, update `CONTROLLER_WS_URL` in `/etc/default/s905x-agent` (or pin via router reservation). |
| Miner won't start with `-j 4 -c 3` | That config pins hash thread #4 onto the control core's core. On 4-core boxes keep `-j 3 -c 3` (the default). |

## Known Limitations

- All controller state (fleet, pool, histories) is in-memory only: a controller
  restart wipes it until workers re-authenticate, and resets the configured
  pool to its default — re-apply your pool afterwards.
- Static dashboard assets deploy live from disk, but `server.ts` changes only
  take effect after restarting the controller service.
- Simulator spawn/kill buttons authenticate via REST and therefore do not work
  in production builds where no token is baked into the static bundle.
- The `/opt/s905xMinerMess` path is load-bearing for both systemd units.
- The agent's hardcoded default pool (`solo.ckpool.org`) is only neutralized by
  the `/etc/hosts` guardrail described above; other hostnames are not blocked.

---

## License
This project is open-source software licensed under the **MIT License**.

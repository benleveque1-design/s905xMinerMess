# S905X Bitcoin SHA-256d Mining Fleet

Native C SHA-256d miner for Amlogic S905X (Cortex-A53, ARMv8-A Crypto Extension)
TV boxes, supervised by a Python WebSocket agent and managed by a central
Node.js controller with a React dashboard.

## Overview

```
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
      │  Worker A     │   │  Worker B     │   │  Worker C     │
      │  3 Hash Cores │   │  3 Hash Cores │   │  3 Hash Cores │
      │  1 Ctrl Core  │   │  1 Ctrl Core  │   │  1 Ctrl Core  │
      │  systemd      │   │  systemd      │   │  systemd      │
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

## Prerequisites

### Hardware Requirements

- **Board**: Amlogic S905X TV box (Cortex-A53, 4 cores, ARMv8-A)
- **Storage**: 8GB+ microSD card or internal eMMC
- **Network**: Ethernet (recommended) or WiFi
- **Cooling**: **Active cooling required** (see [Temperature & Cooling](#temperature--cooling))
- **Power**: 5V/2A DC supply

### Software Requirements

- **OS**: Armbian (Debian/Ubuntu) for AArch64
- **Toolchain**: `gcc`, `make`, `git`, `python3`, `python3-websockets`
- **Node.js**: v18+ (for controller only)

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
# Edit .env to set WORKER_AUTH_TOKEN (generate with: openssl rand -hex 32)
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
CONTROLLER_WS_URL=ws://<CONTROLLER_IP>:3010/ws/worker
WORKER_ID=<unique-id>
EOF
sudo chmod 600 /etc/default/s905x-agent

# Install and start
sudo -E ./scripts/install_service.sh
sudo systemctl enable --now s905x-agent
```

## Architecture

### Threading (`-j 3 -c 3`)

The `-j 3 -c 3` configuration is load-bearing:

- **Cores 0–2**: Hash threads (pinned, run SHA-256d computation)
- **Core 3**: Control thread (Stratum session, work distribution, share submission)

Other combinations may cause core contention or leave the control thread unpinned.

### Stratum Protocol

The miner implements Stratum V1:

1. **Subscribe**: Pool assigns unique `extranonce1` per connection
2. **Authorize**: Worker authenticates with pool
3. **Notify**: Pool sends job template (coinb1, coinb2, prevhash, merkle branches)
4. **Submit**: Worker sends found shares (nonce, extranonce2, ntime)

Each miner gets a unique `extranonce1` from the pool, ensuring different block headers even when hashing the same job template.

### Worker-to-Controller Protocol

Communication uses JSON WebSocket messages on `/ws/worker`:

1. **Auth** — worker sends `workerId`, `token`, `cores`, `arch`, `hwCrypto`
2. **Telemetry** — every ~2s: `hashrateMhs`, `tempC`, `cpuFreqMhz`, shares, `pool`
3. **Commands** — controller sends: `start`, `stop`, `restart`, `set_threads`, `set_pool`

## Pool Configuration

### Supported Pools

| Pool | URL | Registration | Notes |
|:---|:---|:---|:---|
| Based Mining | `stratum+tcp://pool.basedmining.xyz:3335` | None (address-based) | Default pool |
| Solo CKPool | `stratum+tcp://solo.ckpool.org:3333` | None (address-based) | Solo mining |
| ViaBTC | `stratum+tcp://btc.viabtc.top:3333` | Required | PPS+/PPLNS/SOLO |
| Braiins | `stratum+tcp://stratum.braiins.com:3333` | Required | Formerly SlushPool |
| F2Pool | `stratum+tcp://btc.f2pool.com:3333` | Required | Large global pool |

### Pool Safety

Default pool is `stratum+tcp://pool.basedmining.xyz:3335`. If no pool is configured, the miner idles with 0 MH/s.

Pool configuration is persistent: saved to `webapp/controller-config.json` and restored on controller restart.

## Mining Performance

| Mode | Hashrate | Notes |
|:---|:---|:---|
| Synthetic benchmark | **10.08 MH/s** | `-j 3 -c 3`, 3 hash threads + 1 control core, 1.512 GHz |
| Real-pool sustained | **8.98–8.99 MH/s** | ~32 min authorized test against external Stratum pool |

All measurements on S905X at 1.512 GHz, no frequency throttling.

## Temperature & Cooling

### ⚠️ CRITICAL: Active Cooling Required

**Every S905X board tested has exceeded safe operating temperature under sustained mining load.** The Amlogic S905X SoC does not have adequate passive cooling for continuous SHA-256d computation at full load.

### Observed Temperatures

| State | Temperature | Notes |
|:---|:---|:---|
| Idle | 42–45 °C | Normal operating range |
| Light load | 55–60 °C | Acceptable |
| Sustained mining (passive) | 69–75 °C | Approaching thermal limit |
| Thermal throttling begins | 80 °C | Clock speed reduced to prevent damage |
| Critical | 90+ °C | Risk of hardware damage |

### Thermal Behavior

- **Throttling**: The S905X automatically reduces CPU frequency when temperature exceeds ~80°C. This causes hashrate to drop significantly (from ~10 MH/s to ~5 MH/s or lower).
- **Hard stop**: The agent enforces thermal hard stop at 80°C — miner is stopped immediately to prevent damage.
- **Recovery**: Once temperature drops below 75°C, mining can resume.

### Required Cooling Solutions

#### 1. Active Fan Cooling (Recommended)

- **USB powered fan** (5V, 30-40mm) attached to the case
- Direct airflow over the SoC/heat sink
- **Best option**: Reduces temperatures by 15-25°C under load
- Ensure fan is running before starting miner

#### 2. Heat Sink + Thermal Pad

- **Aluminum or copper heat sink** (minimum 20mm × 20mm × 5mm)
- Thermal pad (1-2mm thickness) between SoC and heat sink
- Must be in contact with the chip package, not the case
- Reduces temperatures by 5-10°C (insufficient alone for sustained mining)

#### 3. Combined Solution (Best)

- Heat sink attached to SoC with thermal pad
- USB fan providing active airflow over heat sink
- Reduces temperatures by 20-30°C under load
- **This is what every board in our fleet requires**

### Thermal Monitoring

The agent continuously monitors temperature via:
- `/sys/class/thermal/thermal_zone0/temp`
- `/sys/devices/virtual/thermal/thermal_zone0/temp`
- `/sys/class/hwmon/hwmon0/temp1_input`

Temperature is reported to the controller in real-time. If temperature exceeds 80°C, the miner is automatically stopped.

### Recommendations

1. **Never run mining without active cooling** — even with a heat sink, temperatures will exceed 80°C
2. **Monitor temperatures** — check dashboard or logs regularly
3. **Ensure fan is connected before starting miner** — startup heat spike can trigger thermal shutdown
4. **Consider ambient temperature** — summer/hot environments may require additional cooling
5. **If chip lacks a heat sink, install one** — the SoC package is designed for heat transfer

## CLI Reference (`bitcoin_sha256d_s905x`)

| Flag | Long Argument | Default | Description |
|:---|:---|:---|:---|
| `-M` | `--mine` | — | Run live Stratum V1 Bitcoin miner |
| `-P` | `--pool <URL>` | — | Stratum pool URL (`stratum+tcp://host:port`) |
| `-u`, `-U` | `--user <USER>` | — | Stratum worker username / BTC address |
| `-p` | `--pass`, `--password` | `x` | Stratum worker password |
| `-j` | `--threads <N>` | `3` | Number of parallel hashing worker threads |
| `-c` | `--control-core <N>` | `3` | Core index dedicated to control plane |
| `-o` | `--offset <N>` | `0` | First CPU core to pin hashing threads to |
| | `--no-pin` | — | Disable CPU pinning |
| `-t` | `--test` | — | Run correctness tests |
| `-b` | `--benchmark` | — | Run synthetic benchmark |
| `-n` | `--iterations <N>` | `5000000` | Benchmark iterations per thread |
| `-x` | `--spin-core <N>` | off | Diagnostic: busy-spin thread on core N |
| `-y` | `--spin-duty <P>` | `100` | Duty cycle percent for spin thread |
| `-m` | `--spin-mode <M>` | `alu` | Spin workload: alu, loadstore, membw, neon, sha |
| `-s` | `--sw-only` | — | Force software fallback (disable HW crypto) |
| `-h` | `--help` | — | Display help |

## Deployment

### Controller Setup

1. **Install dependencies**:
   ```bash
   cd webapp
   npm install
   ```

2. **Configure environment**:
   ```bash
   cp .env.example .env
   # Edit .env and set WORKER_AUTH_TOKEN
   chmod 600 .env
   ```

3. **Build and start**:
   ```bash
   npm run build
   npm start
   ```

4. **Optional: Install as systemd service**:
   ```bash
   sudo cp scripts/s905x-controller.service /etc/systemd/system/
   sudo systemctl enable --now s905x-controller
   ```

### Worker Setup

1. **Install toolchain**:
   ```bash
   sudo apt-get update
   sudo apt-get install -y gcc make git python3-websockets libc6-dev
   ```

2. **Clone and build**:
   ```bash
   cd /opt
   sudo git clone https://github.com/benleveque1-design/s905xMinerMess.git
   cd s905xMinerMess/s905x-miner
   make
   ```

3. **Configure agent**:
   ```bash
   sudo tee /etc/default/s905x-agent <<EOF
   WORKER_AUTH_TOKEN=<same token as controller>
   CONTROLLER_WS_URL=ws://<CONTROLLER_IP>:3010/ws/worker
   WORKER_ID=<unique-id-for-this-node>
   EOF
   sudo chmod 600 /etc/default/s905x-agent
   ```

4. **Install and start service**:
   ```bash
   sudo -E ./scripts/install_service.sh
   sudo systemctl enable --now s905x-agent
   ```

5. **Verify**:
   ```bash
   sudo systemctl status s905x-agent
   sudo journalctl -u s905x-agent -f
   ```

### Systemd Unit

The systemd unit hardcodes absolute paths under `/opt/s905xMinerMess/s905x-miner`. The repo must be checked out at exactly that path.

## Development

### Webapp

```bash
cd webapp
npm run dev          # Vite HMR dev server
npm run lint         # TypeScript check (tsc --noEmit)
npm run build        # Production build
```

### Miner

```bash
cd s905x-miner
make                 # Build
make test            # Run test suite
make benchmark       # Run benchmark
```

### Architecture Notes

- Controller state is **in-memory only** — restart wipes fleet and pool config
- Workers auto-reconnect within seconds of controller restart
- Pool config is persistent (saved to `controller-config.json`)
- Python agent requires `python3-websockets` (apt package, not pip)

## Troubleshooting

### Miner won't start

- Check that `WORKER_AUTH_TOKEN` is set in `/etc/default/s905x-agent`
- Verify miner binary exists: `ls -la /opt/s905xMinerMess/s905x-miner/bitcoin_sha256d_s905x`
- Check logs: `sudo journalctl -u s905x-agent -f`

### High temperature / throttling

- Check if fan is connected and running
- Verify heat sink is properly attached with thermal pad
- Monitor temperature: `cat /sys/class/thermal/thermal_zone0/temp`
- If temperature exceeds 80°C, miner will stop automatically

### No shares accepted

- Verify pool URL and credentials in dashboard
- Check network connectivity: `ping <pool-host>`
- Ensure BTC address is valid and properly formatted

### Worker not connecting to controller

- Verify `CONTROLLER_WS_URL` in `/etc/default/s905x-agent`
- Check controller is running: `curl http://<controller-ip>:3010/api/health`
- Ensure firewall allows outbound WebSocket (port 3010)

## License

MIT License

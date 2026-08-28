# AGENTS.md

Two self-contained projects sharing one repo — no shared code between them:

- `s905x-miner/` — Native C SHA-256d miner + Python WebSocket supervisor (`agent/s905x_agent.py`). Targets AArch64 S905X boxes (Amlogic S905X, Cortex-A53, Armbian); builds/runs on x86 too via software fallback.
- `webapp/` — React dashboard + Express/WS controller (`webapp/server.ts`). The only Node package; root `package.json` only delegates via `npm --prefix webapp`. Never add deps at the root.

## Fleet topology (verified 2026-08-28)

| Node | IP | Hostname | Worker ID | SSH | Role |
|------|-----|----------|-----------|-----|------|
| Controller | 192.168.1.156 | printserver | — | key | Runs webapp (Node.js + React dashboard) |
| `.208` | 192.168.1.208 | aml-s9xx-box | s905x-node-208 | ben/Cheese | Miner (agent running as standalone process, not systemd) |
| `.118` | 192.168.1.118 | forge-box | s905x-forge-118 | ben/Cheese | Miner (agent via systemd s905x-agent.service) |
| `.224` | 192.168.1.224 | recovery-box | s905x-recovery-224 | ben/Cheese | Miner (agent via systemd s905x-agent.service) |

All three S905X nodes have ARMv8 Crypto Extension (sha1/sha2 in `/proc/cpuinfo`).
The controller is the Dell dev machine running Armbian/Ubuntu at `.156`.

### SSH access

- **User:** `ben` on all nodes (password: `Cheese`)
- **Root:** `root` on `.118` and `.224` (password: `Cheese`); root on `.208` not confirmed
- **Key:** dev box `~/.ssh/id_ed25519` (comment: `rescue@s905x-devbox`) is installed on all three nodes via `ssh-copy-id`
- **SSH config entries:**
  - `s905x-208` → `ben@192.168.1.208` (key: `~/.ssh/id_ed25519`)
  - `.118` and `.224` use plain `ssh ben@192.168.1.1xx` (no named config entry yet)

## Commands

- C tests/benchmark: `make test` / `make benchmark` (root Makefile delegates into `s905x-miner/`). Directly: `./bitcoin_sha256d_s905x -t` (6 test groups incl. midstate paths and Stratum parser regressions) and `-b -n <iterations> -j 3`.
- `s905x-miner` Makefile auto-detects arch: aarch64 adds `-march=armv8-a+crypto` (ARMv8 SHA intrinsics); x86 builds generic fallback. Code must keep compiling/passing tests both ways.
- Webapp has **no test framework** — verification is `npm run lint` (which is really `tsc --noEmit`) plus `npm run build` (Vite SPA + esbuild bundle of `server.ts` → `dist/server.cjs`; root build also copies `webapp/dist` → `./dist`).

## Controller

- Runs as systemd unit `s905x-controller` on **port 3010** on the Dell (192.168.1.156).
- `User=print` — binds unprivileged port, reads `.env`/`dist` from the checkout.
- State is **in-memory only**: restarting the controller wipes fleet and pool. Workers auto-reconnect within seconds.
- After any controller restart, re-apply the sandbox pool (or your chosen pool) via the dashboard pool editor or `POST /api/pool` with the token.
- `.env` lives at `webapp/.env` with `WORKER_AUTH_TOKEN` (chmod 600).
- To restart: `kill <PID>` and systemd restarts it (`Restart=always`), or `sudo systemctl restart s905x-controller`.

## Server gotchas

- Controller **exits at startup if `WORKER_AUTH_TOKEN` is unset**. Copy `webapp/.env.example` → `webapp/.env` first.
- Always start the server through npm scripts (`npm run dev` / `npm start` from root or `webapp/`) so cwd is `webapp/`: dotenv loads `.env` from cwd, and production mode serves static files from `process.cwd()/dist`.
- `.env.example` sets `NODE_ENV=production`; with it set, `npm run dev` serves the stale static `dist/` instead of Vite middleware (no HMR). Comment it out for development.
- One port (default 3010) carries REST API, `/ws/client` (dashboard), and `/ws/worker` (agents).
- Mutating POST endpoints require header `x-auth-token: $WORKER_AUTH_TOKEN` (timing-safe compare); GETs are open.
- Three simulated workers start automatically for zero-setup previewing and are destroyed when the first real worker authenticates on `/ws/worker`. Extra sims: `POST /api/simulator/spawn|kill`.

## Miner threading architecture (`-j 3 -c 3` is load-bearing)

- Core 3 is not spare capacity: a dedicated control thread pinned there runs the entire Stratum V1 session — socket/reconnects, JSON-RPC parsing, work distribution to hash threads, and share submission (bitcoin_sha256d_s905x.c:1731–1765). Hash threads only hash and enqueue shares.
- Hash threads pin sequentially from `-o`: `cpu_id = core_offset + i` with no overlap check against the control core. `-j 4 -c 3` pins hash thread #4 onto the control thread's core; `-j 4` alone leaves the control thread unpinned (`cpu_id < 0` skips affinity). The agent's `set_threads` clamps only to `[1, maxCores]` (s905x_agent.py:266), so the dashboard can request exactly this broken config — keep defaults on 4-core boxes. Non-4-core hosts default to N-1 hash + last core as control.

## Agent & miner quirks

- Python agent requires distro `python3-websockets` (apt) and refuses to start otherwise — no runtime pip installs. Config via env/flags: `CONTROLLER_WS_URL`, `WORKER_AUTH_TOKEN`, `WORKER_ID`, `MINER_BIN`.
- Linux process `comm` is truncated to **15 characters** (`bitcoin_sha256d_s905x` → `bitcoin_sha256d`), so `pgrep -x`/`pkill -x` with the full name silently matches nothing. Match with `pgrep -f "bitcoin_sha256d_s905x --mine"` instead. Beware `pkill -f` self-matching its own wrapper shell — use a `[b]racket` pattern or PID.
- Agent default `--id` is `s905x-real-01` (s905x_agent.py:516). Each node must set a unique `WORKER_ID` in `/etc/default/s905x-agent` to avoid collisions.

## Deployment (worker nodes)

1. Install toolchain: `sudo apt-get install -y gcc make git python3-websockets libc6-dev`
2. Clone repo: `cd /opt && sudo git clone https://github.com/benleveque1-design/s905xMinerMess.git`
3. Build: `cd /opt/s905xMinerMess/s905x-miner && make`
4. Configure `/etc/default/s905x-agent`:
   ```
   WORKER_AUTH_TOKEN=<same token as controller>
   CONTROLLER_WS_URL=ws://192.168.1.156:3010/ws/worker
   WORKER_ID=<unique-id-for-this-node>
   ```
5. Install systemd service: `sudo -E ./scripts/install_service.sh` (or manually create `/etc/systemd/system/s905x-agent.service`)
6. Start: `sudo systemctl enable --now s905x-agent`

The systemd unit hardcodes absolute paths under `/opt/s905xMinerMess/s905x-miner` — the repo must be checked out at exactly that path.

## Pool sandboxing

- Default pool is `stratum+tcp://127.0.0.1:9333` (dead endpoint) — miner idles in retry loop, hashrate = 0, CPU ~1%. This is expected, not a bug.
- `/etc/hosts` guardrail: `127.0.0.1 solo.ckpool.org` — redirects only that hostname; other pools resolve normally.
- `.208` currently mines against `pool.basedmining.xyz:3335` (real pool, set before this deployment). The other two nodes use the sandbox pool.
- After any controller restart, re-apply the sandbox pool via the dashboard or `POST /api/pool`.

## Real-pool testing rules (hard requirements)

- Real-pool tests require **explicit owner authorization** each time; restore the sandbox pool afterwards and verify restoration (args, connections, globalPool).
- Never weaken or remove the `/etc/hosts` `solo.ckpool.org` guardrail merely to reach another authorized hostname — it only pins that one hostname; other pools resolve normally without touching it.
- Thermal hard stops stay absolute regardless of monitoring sophistication: temp ≥ passive trip (80 °C on this box) or cooling-device `cur_state > 0` → stop immediately.
- Frequency-drop checks must distinguish **startup/Stratum-handshake idle dips** from genuine throttling: require an established mining connection AND several consecutive low-frequency samples before treating low clock as thermal evidence. A naive rule false-triggers within seconds of miner restart (seen live).

## Protocol contract

The JSON WebSocket protocol (auth/telemetry/command message shapes) in root README §"WebSocket Protocol Contract" is implemented three times: C stratum client, Python agent, TS controller. Any protocol change must update all three implementations and the README section.

## Package manager

A `bun.lock` sits at the repo root, but everything here uses npm (`webapp/package-lock.json`, `npm --prefix webapp` scripts). Use npm.

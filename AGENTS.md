# AGENTS.md

Two self-contained projects sharing one repo — no shared code between them:

- `s905x-miner/` — Native C SHA-256d miner + Python WebSocket supervisor (`agent/s905x_agent.py`). Targets AArch64 S905X boxes (Armbian); builds/runs on x86 too via software fallback.
- `webapp/` — React dashboard + Express/WS controller (`webapp/server.ts`). The only Node package; root `package.json` only delegates via `npm --prefix webapp`. Never add deps at the root.
- `reference/esp32/` — optional Arduino sketch; not part of either deployment.

## Commands

- C tests/benchmark: `make test` / `make benchmark` (root Makefile delegates into `s905x-miner/`). Directly: `./bitcoin_sha256d_s905x -t` (7 correctness checks incl. Stratum parser regressions) and `-b -n <iterations> -j 3`.
- `s905x-miner` Makefile auto-detects arch: aarch64 adds `-march=armv8-a+crypto` (ARMv8 SHA intrinsics); x86 builds generic fallback. Code must keep compiling/passing tests both ways.
- Webapp has **no test framework** — verification is `npm run lint` (which is really `tsc --noEmit`) plus `npm run build` (Vite SPA + esbuild bundle of `server.ts` → `dist/server.cjs`; root build also copies `webapp/dist` → `./dist`).

## Verification environments (don't assume x86-only)

- A real S905X box is reachable from the dev machine as `ssh s905x` (key auth, gcc/make present). The repo is NOT deployed there (`/opt/s905xMinerMess` absent; loose miner copies under `~` are stale vs repo). To test ARM-specific behavior: `scp` the current source to `/tmp/`, compile natively with `-march=armv8-a+crypto`, run `-t` / small `-b`. Verified end-to-end: suite passes on-device and `-b` engages the ARMv8 Crypto Extension backend (~8.4 MH/s). Clean up `/tmp` afterwards.
- Neither the S905X nor this dev box has `python3-websockets` yet (`sudo apt install python3-websockets` unblocks; package exists in apt), so agent-in-the-loop E2E is currently unverified — don't claim it without installing first.
- The controller and miner binary both run on this dev box: exercise REST/WS on localhost with a temp `PORT` + throwaway token (auto-started sims make it zero-setup).

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

## Webapp functional state

- Real-worker path is fully implemented end-to-end, not mocked: token auth on `/ws/worker` (bad token → close 4001), `auth_ack` carrying pool+threads, ~2s telemetry ingestion, command acks/logs relayed to dashboards, OFFLINE after >8s silence (server.ts:390–505). Root README documents live verification against real hardware.
- Dashboard controls: per-worker start/stop/restart/thread slider/rename, fleet-wide start/stop/restart, pool edit+broadcast, simulator spawn/kill (App.tsx). Commands go over the dashboard WS; the REST POST fallback needs `VITE_WORKER_AUTH_TOKEN` baked in at build time or it gets 401s.
- Simulated workers emit fabricated telemetry (fake IPs, simulated thermal throttling) but accept the same command set via the same dispatch path.
- All controller state (fleet, pool, histories) is in-memory only — restarting the controller wipes it until workers re-auth.

## S905X deployment (worker boxes)

- The systemd unit hardcodes absolute paths under `/opt/s905xMinerMess/s905x-miner` (systemd/s905x-agent.service:9,15–17) — the repo must be checked out at exactly that path on the box or the service can't start.
- Flow: build with `make` → export `WORKER_AUTH_TOKEN` (+ `CONTROLLER_WS_URL`) → `sudo -E ./scripts/install_service.sh`. Installer requires root, pre-checks python3-websockets, provisions `/etc/default/s905x-agent` (chmod 600; an existing file is kept as-is), then enables+restarts the unit. Unit runs the agent with `--autostart` (mining starts at boot) and `Restart=always`.
- No systemd unit ships for the controller side — root README §Quick Deployment shows the inline unit to create (requires `npm run build` first; `WorkingDirectory=/opt/s905xMinerMess/webapp`, `ExecStart=node dist/server.cjs`).

## Protocol contract

The JSON WebSocket protocol (auth/telemetry/command message shapes) in root README §"WebSocket Protocol Contract" is implemented three times: C stratum client, Python agent, TS controller. Any protocol change must update all three implementations and the README section.

## Package manager

A `bun.lock` sits at the repo root, but everything here uses npm (`webapp/package-lock.json`, `npm --prefix webapp` scripts). Use npm.

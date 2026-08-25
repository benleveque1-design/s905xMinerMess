# AGENTS.md

Two self-contained projects sharing one repo — no shared code between them:

- `s905x-miner/` — Native C SHA-256d miner + Python WebSocket supervisor (`agent/s905x_agent.py`). Targets AArch64 S905X boxes (Amlogic S905X, Cortex-A53, Armbian); builds/runs on x86 too via software fallback.
- `webapp/` — React dashboard + Express/WS controller (`webapp/server.ts`). The only Node package; root `package.json` only delegates via `npm --prefix webapp`. Never add deps at the root.

## Commands

- C tests/benchmark: `make test` / `make benchmark` (root Makefile delegates into `s905x-miner/`). Directly: `./bitcoin_sha256d_s905x -t` (6 test groups incl. midstate paths and Stratum parser regressions) and `-b -n <iterations> -j 3`.
- `s905x-miner` Makefile auto-detects arch: aarch64 adds `-march=armv8-a+crypto` (ARMv8 SHA intrinsics); x86 builds generic fallback. Code must keep compiling/passing tests both ways.
- Webapp has **no test framework** — verification is `npm run lint` (which is really `tsc --noEmit`) plus `npm run build` (Vite SPA + esbuild bundle of `server.ts` → `dist/server.cjs`; root build also copies `webapp/dist` → `./dist`).

## Verification environments (don't assume x86-only)

- A real S905X box is reachable from the dev machine as `ssh s905x` (key auth, gcc/make present). It is now deployed at `/opt/s905xMinerMess` (git clone; the older loose miner copies under `~benabus` are stale vs repo). To test ARM-specific behavior: `scp` the current source to `/tmp/`, compile natively with `-march=armv8-a+crypto`, run `-t` / small `-b`. Verified end-to-end: suite passes on-device and `-b` engages the ARMv8 Crypto Extension backend (~8.4 MH/s on a short default-length run; see the duration caveat under Measured real-pool baseline). Clean up `/tmp` afterwards.
- The dev box has distro `python3-websockets` (10.4), so full agent↔controller E2E runs locally: start the controller on a temp `PORT` + throwaway token, attach a scratch `/ws/client` listener to observe ack broadcasts, then run `python3 agent/s905x_agent.py --server ws://127.0.0.1:<port>/ws/worker --id X --token <tok> --miner ./bitcoin_sha256d_s905x`. Verified live: auth (bad token → close 4001), ~2s telemetry ingestion, and `ping`/`rename`/`set_pool`/`start`/`stop` commands with `command_ack` relayed back. Point `set_pool` at a dead local port (e.g. `stratum+tcp://127.0.0.1:9333`) so the miner retries locally instead of hitting a real pool. Launch long-lived test processes with `setsid nohup … & </dev/null` or they die when the shell session closes.
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
- Linux process `comm` is truncated to **15 characters** (`bitcoin_sha256d_s905x` → `bitcoin_sha256d`), so `pgrep -x`/`pkill -x` with the full name silently matches nothing. Match with `pgrep -f "bitcoin_sha256d_s905x --mine"` instead. Beware `pkill -f` self-matching its own wrapper shell — use a `[b]racket` pattern or PID.

## Real-pool testing rules (hard requirements)

- Real-pool tests require **explicit owner authorization** each time; restore the sandbox pool afterwards and verify restoration (args, connections, globalPool).
- Never weaken or remove the `/etc/hosts` `solo.ckpool.org` guardrail merely to reach another authorized hostname — it only pins that one hostname; other pools resolve normally without touching it.
- Thermal hard stops stay absolute regardless of monitoring sophistication: temp ≥ passive trip (80 °C on this box) or cooling-device `cur_state > 0` → stop immediately.
- Frequency-drop checks must distinguish **startup/Stratum-handshake idle dips** from genuine throttling: require an established mining connection AND several consecutive low-frequency samples before treating low clock as thermal evidence. A naive rule false-triggers within seconds of miner restart (seen live).

## Measured real-pool baseline (authorized extended test, ~32.4 min, 2026-08-21/22)

- Sustained hashrate **8.98–8.99 MH/s** at a locked **1.512 GHz**; 2 accepted / 0 rejected shares against `pool.basedmining.xyz:3335`.
- Thermal equilibrium **~69–70 °C** (reached after ~25 min, held through end of test); peak **70.0 °C** vs kernel trips **80/90/110 °C** (passive/hot/critical); zero cooling engagement; zero sub-max frequency samples while connected.
- The earlier ~5½-minute test's apparent **64 °C plateau is NOT the equilibrium** — do not cite it as such. Short synthetic runs also read lower/higher than sustained mining (e.g. ~8.4 MH/s short `-b`, 10.08 MH/s long `-b`): always state the test duration when quoting numbers.
- This demonstrates ~32 minutes of stable equilibrium under tested ambient conditions only; a multi-hour soak would be needed for any indefinite-reliability claim.

## Webapp functional state

- Real-worker path is fully implemented end-to-end, not mocked: token auth on `/ws/worker` (bad token → close 4001), `auth_ack` carrying pool+threads, ~2s telemetry ingestion (incl. per-worker `name`/`pool` updates from rename/set_pool), command acks/logs relayed to dashboards, OFFLINE after >8s silence (server.ts:451+, offline check at server.ts:330). Verified live against the real Python agent — see Verification environments. Root README documents live verification against real hardware.
- Dashboard controls: per-worker start/stop/restart/thread slider/rename, fleet-wide start/stop/restart, pool edit+broadcast, simulator spawn/kill (App.tsx). Worker commands and pool broadcasts travel **only over the dashboard WS** — the token is deliberately not baked into static builds, so mutating REST from the browser can't authenticate. `sendCommand` waits up to 5s for a reconnecting socket and toasts on failure instead of silently dropping (App.tsx). Dashboard `set_pool` also updates controller-side `globalPool` + broadcasts `pool_update` (server.ts, same semantics as POST /api/pool) — verified live 2026-08-21.
- Simulator spawn/kill buttons are still REST-only → they fail with silent 401s in production unless `VITE_WORKER_AUTH_TOKEN` was baked at build time (it isn't). Harmless in prod; works in local dev.
- Simulated workers emit fabricated telemetry (fake IPs, simulated thermal throttling) but accept the same command set via the same dispatch path.
- While sandboxed against the dead port (`127.0.0.1:9333`), the miner reports hashrate 0 and idles at ~1% CPU in its stratum retry loop — dashboard showing 0 MH/s + cool box is EXPECTED there, not a bug.
- All controller state (fleet, pool, histories) is in-memory only — restarting the controller wipes it until workers re-auth.

## S905X deployment (worker boxes)

- The systemd unit hardcodes absolute paths under `/opt/s905xMinerMess/s905x-miner` (systemd/s905x-agent.service:9,15–17) — the repo must be checked out at exactly that path on the box or the service can't start.
- Flow: build with `make` → export `WORKER_AUTH_TOKEN` (+ `CONTROLLER_WS_URL`) → `sudo -E ./scripts/install_service.sh`. Installer requires root, pre-checks python3-websockets, provisions `/etc/default/s905x-agent` (chmod 600; an existing file is kept as-is), then enables+restarts the unit. Unit runs the agent with `--autostart` (mining starts at boot) and `Restart=always`.
- No systemd unit ships for the controller side — root README §Production Deployment shows the inline unit to create (requires `npm run build` first; `WorkingDirectory=/opt/s905xMinerMess/webapp`, `ExecStart=node dist/server.cjs`).

## Production deployment (verified live 2026-08-21)

- Controller runs as a systemd unit `s905x-controller` on **port 3010** (other homelab services own common ports — don't move it). It safely runs `User=print`: it binds an unprivileged port and only reads `.env`/`dist` under the home checkout; `/opt/s905xMinerMess` is a symlink to the real repo, so there's one copy.
- **Deploy order matters**: both units reference absolute paths under `/opt/s905xMinerMess`. Installing/enabling before that path exists causes a systemd CHDIR failure restart-loop (seen live with the agent). Put code at the path first, then install.
- Token flow: generate once into `webapp/.env` (chmod 600); transfer with `scp -p webapp/.env box:~/.s905x-agent-env`, source it for `sudo -E ./scripts/install_service.sh`; installer writes `/etc/default/s905x-agent` (600 root). Delete the home copy afterwards.
- **Pool sandboxing**: the agent's `--autostart` spawns the miner with hardcoded `solo.ckpool.org` *before* any controller contact can override it. Guardrail on the worker: `/etc/hosts` line `127.0.0.1 solo.ckpool.org` (remove only when authorizing a real pool). Also re-apply the sandbox pool after EVERY controller restart — globalPool resets to its default because state is in-memory; the dashboard pool editor now does this over WS (no token needed), or `POST /api/pool` with the token.
- **Deploying controller changes**: `npm run build` swaps static SPA assets live (express serves from disk, no restart), but `server.ts` changes only take effect after `sudo systemctl restart s905x-controller` — Node keeps the old `server.cjs` in memory until restarted (seen live 2026-08-21). Restart wipes in-memory state → re-apply sandbox pool immediately after.
- Controller restarts wipe fleet+pool in memory; real workers auto-reconnect within seconds and sims return until then. The agent service itself survives controller restarts untouched (WS backoff reconnect).
- Agent journal appears empty because python stdout is block-buffered under systemd; add `PYTHONUNBUFFERED=1` to the unit if logs are wanted (root change). `systemctl restart s905x-agent` needs root on the box; crashes auto-respawn via `Restart=always`.
- Server ufw is `ENABLED=no` (not enforcing) → no firewall change needed for LAN access. Server LAN IP is DHCP (`192.168.1.156`) — if it changes, update `CONTROLLER_WS_URL` in `/etc/default/s905x-agent`; pin it via router reservation only with owner approval.

## S905 (GXBB) node `.243` — second SoC family (integrated 2026-08-24)

- `192.168.1.243` = Amlogic Meson **GXBB** P201 ("aml-s9xx-box", fresh Armbian trixie, kernel 6.18). Root ssh works with existing dev keys; `benabus` does NOT exist there. Repo staged at `/opt/s905xMinerMess` (tar copy, not git).
- **No ARMv8 crypto extension** (`Features` lacks sha1/sha2) → runtime HWCAP dispatch picks the software backend (~1 MH/s at `-j 3`, ~330 KH/s/core vs 3.3 MH/s/core HW on S905X). The Makefile no longer force-adds global `+crypto`; HW fns opt in per-function via `target("+crypto")` — one binary serves both SoCs.
- **Thermal sensor is dead on this box**: zone0 permanently reads `-1000` m°C (hwmon raw 0xFFFFFFFF×1000; legacy SCPI pre-1.0 firmware returns an invalid sentinel — and −1000 m°C = −1.0 °C passes naive range checks; agent rejects ≤0 °C readings and sends `tempC: null`). Thermal evidence for this box = sustained clocks only.
- `cpuinfo_max_freq`=2016 but `scaling_max_freq` clamped to 1536 (origin UNKNOWN, left as-is); governor ondemand.
- Agent runs from a detached shell (NOT systemd yet) with prod token, id `s905-node-243`. trixie's python3-websockets **15.0.1 works** with the agent (legacy connect API still functional). Toolchain installed with owner approval: gcc/make/git/python3-websockets/libc6-dev.
- Guardrails identical to fleet: `/etc/hosts` pins solo.ckpool.org→127.0.0.1; controller globalPool kept at Sandbox dead-port pool (re-apply after any controller restart).
- Benchmarks (33-min real-pool + 31–33-min synthetic, both boxes): `.120` 9.02 MH/s real-pool / 10.08 MH/s synthetic; `.243` 0.99 MH/s both modes; zero throttling anywhere (clocks flat at ceiling). Full methodology+results: `docs/s905-integration.md`.
- Watchdog lessons: throttle detection must use each box's own measured healthy baseline under the exact workload (owner requirement), integer math only (bash float arithmetic is fatally fatal non-interactively), and miner exit ends the test (post-completion idle downclock false-trips deviation checks).

## Minimal-stack investigation (2026-08-22; details in docs/s905x-minimal-stack.md, live stripping in docs/s905x-strip-experiment-2026-08-22.md)

- Live stripping ran on `s905x2` (second box, benabus2, passwordless sudo, 192.168.1.246) and is **COMPLETE (2026-08-22)**: A1 services + A2 timers + pulseaudio autospawn kill + A3 swap-off (ramlog kept) + A4 purge (1375→733 pkgs) + B1 initramfs removal — all reboot-verified. Final: boot **19.8→12.2 s**, running units 44→25, idle RAM **324→~210 MiB**, no swap, idle CPU 0.3%. Rollback snapshots on-box `~/strip-rollback/` + repo `docs/artifacts/strip-s905x2/` (incl. pre/post extlinux.conf; uInitrd file left in place for one-line B1 revert). Gotchas recorded there: `root=LABEL=` does NOT work without initrd (switched to PARTUUID); miner CLI rejects glued short options (`-j3` → use `-j 3`); python3-websockets was never installed on s905x2 until now. NTP is LAN-blocked on BOTH boxes (pre-existing; chrony Reach 0) — boxes run fake-hwclock time.

- Baseline on the box: synthetic `-b -n 200000000` = 9.96–10.02 MH/s at `-j3`, single-core 3.344 MH/s, scaling ratio 2.985 ⇒ hash threads already run free of userspace interference; don't expect hashrate gains from stripping userspace/kernel — the wins there are RAM/boot-time only.
- Miner's complete kernel-facing surface is tiny (`nm -D` census in docs): pthreads/affinity, timers, TCP, sysfs reads of `thermal_zone0/temp` + `scaling_cur_freq`. Everything else in Armbian is convenience.
- A minimal S905X kernel was cross-built locally (6.18.45 + fragment: ARCH_MESON/SERIAL_MESON/MMC_MESON_GX/EXT4/GXBB clocks/PINCTRL_GXL/CPUFREQ_DT/AMLOGIC_THERMAL/STMMAC): **Image 10.3 MB vs 42.4 MB stock, zero modules** — but it has never been booted; boot-testing needs a spare node or owner authorization.
- Live service-stop experiments on the box are blocked by polkit (root required); a self-restoring script exists (`~/s905x-invest/reduce_userspace.sh` on the dev box) and needs `sudo bash` to run. Never assume systemctl stop works over plain SSH as benabus.
- DRAM init for S905X lives in vendor U-Boot/TF-A blobs — any below-U-Boot work means re-implementing that; pragmatic floor is ROM→U-Boot kept, replace kernel+userspace above it. Bare-metal verdict: achievable but ~weeks of work for zero expected hashrate/per-watt gain (see docs).
- No power meter exists in this environment — hashes/sec/watt claims cannot be measured yet; don't fabricate them.

## Phase-2 appliance-minimum experiment (2026-08-22; ⚠ s905x2 currently LOCKED OUT)

- Phase 2 (minimum userspace for the miner appliance) ran on `s905x2` and produced verified results before an incident: E1–E4 service/timer changes (masked alsa-restore; disabled NM-wait-online/dispatcher, console/keyboard-setup, setvtrgb, dmesg, systemd-pstore, e2scrub_reap, getty@tty1 — serial-getty@ttyAML0 KEPT; chrony stop+disable; resolved→NM `rc-manager=file` via `/etc/NetworkManager/conf.d/90-miner-resolv.conf` with `/etc/resolv.conf` as real file 9.9.9.9+1.1.1.1; cron→`armbian-truncate-logs.{service,timer}` OnBootSec=5min OnUnitActiveSec=15min) + E5 purge (explicit chrony+toilet, then autoremove −329 pkgs). Verified after reboot: **733→366 pkgs, 2214→1374 MiB installed, rootfs 46%→31%, boot 12.2→11.15s, units 25→21, RAM ~192 MiB, function matrix 18/18 PASS** (`docs/artifacts/strip-s905x2/fnmatrix.sh`, restage to /tmp + miner binary from `~/s905x-work/` after every reboot). Key finding: `toilet` (Recommends-only from armbian-bsp-cli) pinned mesa-vulkan/libllvm/mesa-libgallium ≈310 MiB.
- **INCIDENT**: the E5 batch also purged `sudo` (protected-set vetting gap) → benabus2 had no root/polkit/reboot left. The attempted no-reboot fix (raw-block in-place edit of /etc/passwd uid→0000 via disk-group debugfs/dd + cache eviction) locked out sshd pubkey auth BEFORE the revert ran (OpenSSH secure_filename ownership check vs stale file uids); all sessions died with it. **Box otherwise healthy (all six appliance functions unaffected); recovery needs physical access** — exact one-line fix + two procedures in `docs/artifacts/strip-s905x2/RECOVERY-LOCKOUT.md`. After recovery: reinstall sudo, then resume blocked items (E5b armbian-firmware purge ≈−355 MB, final metrics).
- Hard lessons: purge protected-sets must pin `sudo` explicitly; never test identity-flip edits against a live sshd without holding a ControlMaster lifeline session open first (one-shot exec channels all die together); "nothing depends on X" ≠ "X is safe to drop" when X is the recovery path.

## Sibling workspace: S905X/T95 engineering & recovery (2026-08-23)

- New dedicated hardware/recovery workspace lives at **`~/s905x-engineer/`** — its AGENTS.md is the ground truth for eMMC layout, boot chain, USB OTG burning, and recovery tooling for the T95-class boxes (known-good `.120` donor dump refs, hash-verified revert payloads, gated scripts). Consult it before any low-level box surgery; this file stays focused on the miner fleet.
- **T95 box `.206`/`.223` is PARKED — do not count it as miner capacity.** A "headless-boot fix" (extlinux video/EDID rewrite + U-Boot env `init_display=true`, applied via `~/s905x-invest/aml/work/apply-headless.py` + `fixall-live.py`) FAILED on real hardware: instant green screen, loss of OTG/USB-boot/network. Unrecoverable 2026-08-23 with on-hand tooling (no UART adapter; burn-mode ROM window never catchable after boot-partition BL2 restore). Byte-exact revert payloads + revival recipe are staged in `~/s905x-engineer/{revert-payloads,tools}/`.
- Networking trap (affects any fleet monitoring): **192.168.1.65 answers pings via OpenWrt proxy-ARP** — it is the router's own MAC (`b0:b9:8a:73:81:eb`), not a host. Verify boxes by MAC/IP lease (T95's patched MAC: `00:15:18:d7:e9:d0`), never by a single ping reply.
- Hardware facts learned: T95-class boxes DO have a microSD slot (SD is checked before USB in vendor recovery flow — stage rescue images there); ST-Link V2 clones have NO UART pins (need CH340/CP2102); solid-green-HDMI-from-power-on = early boot hang with U-Boot-env-level rescue scripts unreachable.
- Fleet-wide meta-rule adopted after tonight: **prove an escape hatch first** — on ANY box, run the OTG catch test (`lsusb -d 1b8e:c003` across a power cycle) or confirm serial console BEFORE writing env blocks/boot partitions. A box whose rescue paths are untested is one bad write from being parked like `.206`.

## Protocol contract

The JSON WebSocket protocol (auth/telemetry/command message shapes) in root README §"WebSocket Protocol Contract" is implemented three times: C stratum client, Python agent, TS controller. Any protocol change must update all three implementations and the README section.

## SHA-256 hot-path optimization (2026-08-25, `.206` experimental box)

All experiments ran on `.206` (`192.168.1.206`, root/Cheese). Never modify `.120`.

### Optimization history

| Opt | Change | Commit | Result | Cumulative |
|-----|--------|--------|--------|------------|
| Baseline | Original code | — | 10.074 MH/s | — |
| 1 | Direct SHA256 compress calls (HW path) | `c668658` | 9.532 MH/s (−5.4%) | REVERTED `12202e2` |
| 2 | NEON `vrev32q_u8` byte-swap (`be32_store`) | `f676204` | 10.653 MH/s (+5.75%) | +5.75% |
| 3 | Function pointer elimination (HW direct `bl`) | `3408713` | 11.046 MH/s (+3.7%) | +9.7% |
| 4 | Block2 pre-copy + Pass2 padding pre-init | `dc5597e` | 11.819 MH/s (+7.0%) | +17.3% |
| 5 | K-table parameterization (`noipa`) | — | 10.849 MH/s (−8.2%) | REVERTED |

**Current verified baseline: 11.819 MH/s @ 1512 MHz (3 warmed 50M runs, no `-j3`)**

### How benchmarks are run

1. Deploy source to `.206` via `scp` to `/tmp/`
2. Compile: `gcc -O3 -pthread -Wall -Wextra -march=armv8-a+crypto -o bitcoin_sha256d_s905x bitcoin_sha256d_s905x.c`
3. Correctness: `./bitcoin_sha256d_s905x -t` — all 6 test groups must pass
4. Assembly: `gcc -O3 ... -S -o bench.s bitcoin_sha256d_s905x.c`
5. Benchmark: 10M warmup (ensure CPU0 at 1512 MHz), then 3 × 50M iterations; record Hash Rate, CPU0 Freq, Temp
6. Only count warmed runs (CPU0 starts at 1512 MHz); ondemand governor drops to 500 MHz during idle gaps
7. Use pexpect SSH helper `/tmp/opencode/ssh206.py` (no `sshpass` available)

### Key experimental findings

**Opt 1 rejection (function boundary + `target("+crypto")`):** Adding `__attribute__((target("+crypto")))` to `bitcoin_hash_nonce_opt_hw` prevented inlining into `bench_worker` (which lacks the attribute). Result: 5.4% regression. Lesson: `target("+crypto")` on a caller-visible function prevents GCC from optimizing the call site.

**Opt 5 rejection (K-table parameterization):** Added `sha256_compress_hw_k(state, block, K_table)` with `__attribute__((noinline))` to pass the K round constants as a pointer instead of embedding 16 `adrp`+`ldr` pairs per call (24 fewer instructions per compress). Result: **8.2% regression** despite 15% fewer total instructions per hash (287→244).

Root cause: GCC 15.2's interprocedural constant propagation (IPA CP) creates a `.constprop.0` clone of `sha256_compress_hw_k` with K baked in as a static constant — completely defeating the parameterization. Adding `__attribute__((noipa))` prevents cloning but also blocks all cross-function optimizations (register allocation, instruction scheduling, constant propagation across the call boundary). The loop body grew from 23→26 instructions and GCC's scheduling degraded.

**This is the single most important finding: pipeline utilization matters more than instruction count on the in-order Cortex-A53.**

### Architectural analysis

The Cortex-A53 has a **single-issue NEON pipeline** (max 1 NEON instruction/cycle). The compress function has **115 NEON operations** per call. With 2 compresses per hash, the NEON throughput floor is **240 cycles/hash**. Measured: **384 cycles/hash** (62.5% of throughput floor).

The 60% gap above the NEON floor breaks down as:
- **SHA2 dependency chains** (~25%): sha256h(N+1) depends on sha256h2(N) output with ~3-4 cycle latency; 16 groups × 4 cycles = 64 cycles/compress critical path. Algorithmic — cannot be reduced.
- **Pipeline fill gaps** (~12%): Rounds 12-15 have no message schedule work to interleave (only 1 `mov` between SHA pairs, need 2+). Inherent to the algorithm — no source-level remedy.
- **Scheduling/fetch effects** (~23%): ADRP result latency, load-use bubbles, fetch alignment. GCC's `-O3` scheduler handles these; disrupting it (Opt 5) made things worse.

The remaining ~20% overhead beyond the NEON floor includes dual-issue slot conflicts (integer/NEON pairing constraints) and ADRP chain serialization through the K-address register.

### Performance ceiling

**11.819 MH/s is the known-good performance ceiling** for this implementation at stock 1512 MHz. Estimated realistic ceiling (70-75% NEON utilization): ~13-14 MH/s fleet. The gap is dominated by SHA-256 algorithmic constraints and GCC scheduling that cannot be improved from C source without risking regression.

Do not pursue further hot-path optimizations without new hardware evidence (e.g., cycle-accurate profiling) or a different compiler/toolchain.

## Package manager

A `bun.lock` sits at the repo root, but everything here uses npm (`webapp/package-lock.json`, `npm --prefix webapp` scripts). Use npm.

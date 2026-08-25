# S905 (GXBB) Integration — `.243`

Bringing the second SoC family (original Amlogic S905 / Meson GXBB) into a
fleet built around the S905X (Meson GXL). Target: `192.168.1.243`, fresh
Armbian install. Reference machine: `192.168.1.120` (`ssh s905x`), treated as
immutable except approved temporary benchmark modifications.

Status legend: **VERIFIED** = directly measured/observed · **INFERRED** =
concluded from evidence, not directly proven · **UNKNOWN** = unverified.

---

## Hardware / OS characterization of `.243`

| Item | Finding | Status |
|---|---|---|
| Board | Amlogic Meson GXBB P201 (`/proc/device-tree/model`) | VERIFIED |
| CPU | 4× Cortex-A53 r0p4, 128 KiB L1d/L1i each, 512 KiB L2 | VERIFIED |
| ARMv8 crypto ext | **ABSENT**: `Features: fp asimd evtstrm crc32 cpuid` — no `sha1`/`sha2`/`aes`/`pmull` | VERIFIED |
| Kernel | 6.18.45-current-meson64 (#4 SMP PREEMPT), Armbian community 26.11.0-trunk.19, Debian trixie | VERIFIED |
| RAM | 1.9 GiB + 958 MiB swap (swap on) | VERIFIED |
| Storage | eMMC `/dev/mmcblk0p2`, 113 GB rootfs, ~2% used | VERIFIED |
| Max clock | `cpuinfo_max_freq` = 2016 MHz | VERIFIED |
| Effective clock cap | `scaling_max_freq` = 1536 MHz on all 4 CPUs (governor `ondemand`) — something clamps it below DT max | VERIFIED value / UNKNOWN cause |
| Thermal zone0 | reads `-1000` permanently; hwmon0 (`scpi_sensors`, label `aml_thermal`) raw `4294967295000` = `(2^32−1)×1000`; dmesg: "SCP Protocol legacy pre-1.0 firmware" → SCPI returns invalid sentinel for temperature | VERIFIED |
| Cooling devices | cpufreq-cpu0 (max 7), GPU devfreq (max 6); both cur_state=0 at idle | VERIFIED |
| Trip points | 80000/90000/110000 m°C (passive/hot/critical) — same as S905X fleet | VERIFIED |
| Network | eth0 UP, DHCP, DNS OK; wlan0 down | VERIFIED |
| Toolchain (pre-work) | no gcc/make/git, python3 3.13.5 without websockets → apt batch installed gcc 14/make/git/python3-websockets with owner approval | VERIFIED |

**Thermal conclusion:** the S905's thermal reading is unusable in this OS/firmware
combination. The agent reports `tempC: null` and the dashboard renders N/A.
Thermal behavior during benchmarks is instead characterized by sustained-clock
stability (a drop from 1536 MHz under sustained load is the kernel's passive
cooling action via cpufreq cooling device) plus optional physical spot-checks
by the operator.

## Code changes made (project-side)

1. **`s905x-miner/Makefile`** — dropped forced global `-march=armv8-a+crypto`
   on aarch64 builds. The two HW-crypto functions
   (`sha256_compress_hw`, `spin_batch_sha`) carry their own
   `__attribute__((target("+crypto")))`, and runtime
   `getauxval(AT_HWCAP) & HWCAP_SHA2` dispatch selects the backend.
   One binary now serves both SoCs. Verified:
   - x86 test suite passes unchanged;
   - cross-built AArch64 binary objdump audit: 58 SHA instructions total,
     all inside the two attributed functions, none elsewhere (no SIGILL risk);
   - on-box verification on both machines (see below).
2. **Agent** — `arch`/`hwCrypto` detected from `/proc/cpuinfo` (+lscpu model)
   instead of hardcoded `"aarch64 Cortex-A53"`/`True`; impossible temps
   (< −50 °C or > 150 °C, incl. GXBB sentinel) → `tempC: null`;
   agentVersion 2.2.0. Dashboard-embedded copy `agentScript.ts` regenerated.
3. **Controller/dashboard** — `WorkerRecord.tempC: number | null`; auth record
   starts at `null`; telemetry distinguishes absent field vs explicit null;
   temp history only records numeric samples; WorkerCard/Details/Simulator
   render `N/A`; details modal shows real `arch` and honest crypto status.
4. README protocol contract updated accordingly (agent+TS sides; C miner does
   not send temps itself).

## Live integration verification (2026-08-24)

Against a throwaway controller (dev box, port 3199), then production:

- Agent connects from `.243` with **websockets 15.0.1** (trixie) — compat VERIFIED
  live (auth, telemetry loop, command dispatch all exercised).
- `arch` reported: `aarch64 Cortex-A53` (detected via lscpu); `hwCrypto: false`
  — both correct for GXBB. VERIFIED end-to-end into controller broadcasts.
- `tempC` arrives as JSON `null` on `.243`; dashboard renders N/A (requires
  deployed server build; old build shows stale placeholder until restarted).
- Commands verified live over dashboard WS: `set_pool`→dead-port sandbox,
  `start` (miner RUNNING, 3 hash + control core), `rename`, `stop` — acks
  relayed correctly.
- Production: worker id `s905-node-243` visible alongside `.120`
  (`s905x-real-01`) on the prod controller.

## Benchmarks

Methodology: same source commit, same build flags (generic Makefile build),
`-j 3 -c 3`, ≥30 min per run per box, sampler every 10 s (temp where valid,
freq, load). Two modes: extended synthetic `-b` (hashing ceiling) and
authorized real-pool mining (realistic primary; stratum overhead included).
`.120` runs the benchmark binary from `/tmp` (built there, checkout untouched,
production agent paused with owner approval, restored afterwards).

Throttle rule (owner-specified): each run first measures its own healthy
sustained baseline (median min-core frequency over minutes 1.5–5.5 under the
exact workload); abort ALL tests if any box then holds ≥5 % below its own
baseline for 6 consecutive samples (~60 s). `.120` additionally aborts on
absolute thermal evidence (zone0 ≥80 °C or any cooling engagement).
Implementation note: all watchdog math is integer (basis points) — bash
arithmetic on floats (`0.05`) is a fatal expansion error in non-interactive
shells; the first attempt died silently at exactly the baseline-end boundary
because of it. Status files start as `RUNNING` so early deaths are detectable.

Results (2026-08-24, ambient ~room temp, `-j 3 -c 3`, sampler 10 s):

### Real-pool mining — `pool.basedmining.xyz:3335` (owner-authorized), 33 min

| Metric | `.120` S905X (crypto HW) | `.243` S905 (software) |
|---|---|---|
| Sustained hashrate | **9.01–9.02 MH/s** | **0.99 MH/s** |
| Shares | 6 accepted / 0 rejected / 0 stale | 0 / 0 / 0 (expected at ~1 MH/s vs pool diff) |
| Reconnects | 0 | 0 |
| Healthy baseline (measured min 1.5–5.5) | 1512 MHz | 1536 MHz |
| Clocks under load | flat 1512 MHz all 33 min | flat 1536 MHz all 33 min |
| Throttle events | none | none |
| Temp | 50 → 66 °C (still rising slightly at end) | sensor invalid (`null`) |

### Synthetic `-b` (pure SHA-256d, no network), ≥30 min

| Metric | `.120` | `.243` |
|---|---|---|
| Sustained hashrate | **10.083 MH/s** over 30.6 min (1.85e10 hashes) | **0.998 MH/s** over 30.9 min (1.85e9 hashes) |
| Short-run reference | 9.657 MH/s (2e7 hashes) | 0.998 MH/s (2e7 hashes) |
| Clocks under load | flat 1512 MHz (182/182 samples) | flat 1536 MHz |
| Temp curve | 49→60→63→64–65 °C plateau | n/a |

`.120`'s first long synthetic run was flagged `THROTTLE` at t≈1896 s — a false
positive: the miner had *finished* at t=1835 s and idle-downclocked to 500 MHz;
the watchdog counted post-completion idleness as deviation. Fixed by ending
the test when the miner process exits. The recorded run itself was clean.

### Interpretation (apples-to-apples)

- Both boxes hold their ceiling clocks with zero dips under identical
  workload/config; neither throttled in either mode.
- The ~10× gap is architecture, not configuration: crypto-extension SHA-256d
  vs portable C on the same Cortex-A53 microarchitecture at nearly the same
  clock (1512 vs 1536 MHz).
- `.120` real-pool 9.02 MH/s matches the historical authorized baseline
  (8.98–8.99); no regression from the Makefile change.
- `.243`'s per-core software rate ≈ 330 KH/s ≈ one-tenth of `.120`'s HW rate
  per core (~3.36 MH/s).

## Open items / limitations

- UNKNOWN: what sets `.243`'s `scaling_max_freq` to 1536 MHz (DT OPP table is
  capable of 2016). Left as-is per OS-immutability rule; noted that it makes
  clocks nearly apples-to-apples with `.120`'s 1512 MHz anyway.
- `.243` has no usable thermal sensor → long-run thermal safety relies on
  clock monitoring; trip points remain kernel-enforced regardless.
- Swap is enabled on `.243` (fleet S905X boxes vary); left untouched.
- `.120`'s checkout carries small pre-existing local edits (AGENTS.md,
  webapp/server.ts, dated 2026-08-21, before this integration work); found
  and left untouched.
- `.243`'s agent currently runs from a detached shell session, not systemd;
  installing `s905x-agent.service` on `.243` (paths already correct under
  `/opt/s905xMinerMess`) is a remaining owner decision.
- `.243` produced 0 accepted shares in the 33-min pool window — expected at
  ~1 MH/s against that pool's difficulty; connection health was perfect
  (0 rejects / 0 stale / 0 reconnects). Longer windows or lower vardiff
  would yield shares.

## Final state (2026-08-24)

- Production controller sees both real workers: `s905x-real-01` (.120,
  RUNNING, crypto HW) and `s905-node-243` (.243, RUNNING in sandbox, SW
  fallback, temp N/A). Sandbox Guardrail pool re-applied after restarts.
- Benchmark artifacts cleaned from `/tmp` on both boxes; `.120` checkout and
  services back to pre-benchmark state.

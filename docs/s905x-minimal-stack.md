# S905X Minimal Mining Stack Investigation

Date: 2026-08-22. Method: measured baseline on the production node, static analysis
(source + `nm -D` + kernel config), local cross-build of a minimal kernel. Verified
observations and conclusions are labeled separately throughout.

## 1. Current stack, layered (verified)

```
Boot ROM (mask, Amlogic; loads BL2 from eMMC boot0/1 or SD)
  └─ BL2 → BL30/BL31 (ARM Trusted Firmware; SCP, PSCI)
       └─ U-Boot  (/boot/u-boot-s905; DRAM init lives here)
            └─ /boot/extlinux/extlinux.conf
                 └─ Linux 6.18.35-current-meson64  Image=42.4 MB
                    root=/dev/mmcblk1p2 (ext4), console=ttyAML0 + tty0,
                    initrd + plymouth splash
                      └─ systemd: 241 processes, 31 running services,
                         ~492 MB RSS of 907 MB total (+454 MB zram swap)
                           └─ s905x-agent.service
                                └─ bitcoin_sha256d_s905x (glibc-only dynamic link;
                                   ARMv8 Crypto Extension backend via HWCAP_SHA2)
                                     └─ Stratum over plain TCP
```

Board DT: `amlogic,p212` / `amlogic,meson-gxl` (S905X P212 reference).

## 2. Baseline measurements (verified, production box)

Synthetic benchmark `-b -n 200000000` (no pool involved; deterministic sample hash
across all runs):

| Run | Threads | MH/s | Temp start→end |
|-----|---------|------|----------------|
| base 1 | 3 | 10.018 | 41→51 °C |
| base 2 | 3 | 9.994 | 42→52 °C |
| base 3 | 3 | 9.961 | 43→52 °C |
| base 4 | 3 | 9.997 | 44→52 °C |
| base 5 | 3 | 9.988 | 44→53 °C |
| control (ssh session + full desktop active) | 3 | 9.971 | 41→? |
| single-core ref | 1 | 3.344 | 45→48 °C |

* Governor ondemand, 500–1512 MHz; load ramps to max.
* Scaling ratio -j3/-j1 = **2.985** ⇒ hash threads already run essentially free of
  userspace interference (they are pinned to cores 0–2).
* Real-pool sustained reference (prior authorized test): 8.98–8.99 MH/s over 32.4 min.
* Boot time: 6.79 s kernel + 14.55 s userspace = **21.34 s** to multi-user.
* RAM: ~492 MB used idle (241 procs); zram swap 33% used.
* Power draw: **not measurable** — no power meter available in this environment.

## 3. What the miner actually needs from the kernel (verified)

Complete libc symbol census (`nm -D`, undefined symbols): pthreads
(create/join/mutex/setaffinity), clock_gettime, nanosleep/usleep, poll, socket/
connect/send/recv/setsockopt, getaddrinfo, stdio/strtol-family, signal, sysconf —
plus two sysfs reads: `/sys/class/thermal/thermal_zone0/temp` and
`/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq`.

⇒ Miner-required kernel facilities: scheduler + SMP + CPU affinity, futex/timers,
TCP/IP stack, devtmpfs/sysfs, ELF loader, one filesystem for the binary, MMC host
driver, Meson GXL clocks/pinctrl, serial console (recovery), thermal driver
(safety telemetry), cpufreq (performance state).

Everything else in Armbian is Linux/Armbian convenience, not mining need.

## 4. Userspace classification

Required for the mining service: `s905x-agent`, `ssh` (management), a network
manager (`NetworkManager` here) or any DHCP client, `dbus`+`systemd-*`
(framework deps), `serial-getty@ttyAML0` (recovery console). Optional-but-useful:
`chrony`, `rsyslog`/`journald`. Removable (measured RSS): lightdm 34 MB, polkit
18 MB, ModemManager 18 MB, accounts-daemon 16 MB, udisks2 16 MB, upower 15 MB,
unattended-upgrades 14 MB, plus bluetooth/cups/cups-browsed/vnstat/rsync/rtkit/
avahi/getty@tty1/wpa_supplicant (ethernet-only box) — roughly **150 MB RSS and
~8–10 s of boot blame combined**.

Live stop-test result: polkit requires root to stop system units; all stops were
denied, service set verified identical before/after (zero production impact). The
prepared script `reduce_userspace.sh` (stop batch → 2× benchmark → restart batch →
diff verification) is ready but **needs sudo** — owner action required. Expected
outcome based on scaling evidence above: ≈0 hashrate delta; gains would be RAM/boot
only.

## 5. Kernel reduction (built locally, boot untested — no spare node)

Stock config: 6 873 enabled symbols, PREEMPT=y, HZ=250, DEBUG_INFO+BTF,
KPROBES/FTRACE/KEXEC/PSTORE/RAS=y, DRM_MESON+DW_HDMI built-in (display pipeline
initialized every boot), netfilter tables built-in, 254 MB modules tree, ~73
modules loaded at runtime.

Minimal build (this repo's investigation artifact): mainline 6.18.45 +
tinyconfig + explicit fragment (ARCH_MESON, SERIAL_MESON+console, MMC_MESON_GX,
EXT4, TMPFS, DEVTMPFS, BINFMT_ELF, COMMON_CLK_GXBB, PINCTRL_MESON_GXL,
CPUFREQ_DT, AMLOGIC_THERMAL, STMMAC/DWMAC_MESON + REALTEK_PHY, NET/INET/UNIX).
Result: **897 =y symbols, zero modules, no initramfs, Image = 10.3 MB (−76%)**.
Cross-built with `aarch64-linux-gnu-gcc` (host flex/bison/libelf extracted without
root). Fragment preserved at repo-external scratch during investigation; see §8.

Conclusion (hypothesis until boot-tested): hashrate unchanged — the workload is
hardware-limited and already scales perfectly; expected gains are boot time,
RAM footprint, attack surface. The stock kernel's steady-state hashing overhead is
already ≈0 per §2.

## 6. Boot chain / direct hardware access

Verified layout: eMMC 7.3 GB (p1 /boot 488 MB, p2 / 5.9 GB) + hardware boot
partitions boot0/boot1 (4 MB each, U-Boot territory). Chain: ROM → BL2 → BL31 →
U-Boot → extlinux → kernel. DRAM bring-up is inside the vendor U-Boot/TF-A blobs —
any "cut below U-Boot" path means re-implementing/extracting DRAM init (high effort,
fragile per-board). Pragmatic floor: keep ROM→U-Boot as-is, replace everything
above it (kernel/initrd/systemd) with either a minimal kernel (§5) or a freestanding
binary loaded via existing `booti` path.

## 7. Bare-metal feasibility assessment

What bare metal would have to replace (all currently provided by Linux):
MMU/page-table setup, secondary core spin-up (PSCI today), GIC config, generic
timer, cache maintenance, UART driver, MAC/DMA/PHY driver + full TCP/IP + DHCP/DNS
(for standalone Stratum), libc string/format layer, threading (or a per-core
spin-loop design), thermal monitoring (safety!), and watchdog/failsafe logic.
The SHA-256d code itself is portable C + ARMv8 CE intrinsics with midstate
precomputation — reusable nearly as-is.

Verdict: technically achievable (well-documented SoC, mainline drivers as reference,
and keeping U-Boot removes the hardest part), but substantial engineering (~weeks)
for **zero expected hashrate/per-watt gain**: hashing is already at the silicon limit
(10.0 MH/s synthetic ≈ 3.33 MH/s/core × 3 pinned cores; real-pool sustained 8.98
MH/s includes stratum/share overhead). Bare metal buys instant-on boot and minimal
RAM, not performance. Recommendation: do not pursue unless boot-time/robustness is
a goal in itself.

## 8. Artifacts and rollback notes

* Local cross-build tree: `~/s905x-invest/src/linux-6.18.45`; the kernel config
  fragment is preserved in-repo at `docs/artifacts/minimal-meson64.fragment`
  (apply with `scripts/kconfig/merge_config.sh -m .config <fragment>` after
  `make ARCH=arm64 tinyconfig`, then `olddefconfig`).
* Prepared live experiment: `docs/artifacts/reduce_userspace.sh` (run with
  `sudo bash` on the box; self-restoring, diffs unit list before/after; all units
  remain enabled so even a power cut mid-test restores them at next boot).
* Production box untouched by this investigation (read-only probes + benchmarks
  only); sandbox pool and `/etc/hosts` guardrail intact.
* Untested-on-hardware items explicitly flagged: minimal-kernel boot, userspace-stop
  A/B. Both need either a spare S905X node or owner-run commands.

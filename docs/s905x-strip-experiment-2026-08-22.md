# S905X OS-Stripping Experiment — live results (2026-08-22, COMPLETE)

Status: **complete — all planned experiments executed and reboot-verified**. Extends
`docs/s905x-minimal-stack.md` (§4 predicted ~150 MB RSS + 8–10 s boot blame removable; this
doc records the *live* execution on `s905x2`, which has passwordless sudo). `s905x` remains
untouched production reference.

Boxes: `s905x` (benabus, 192.168.1.x, production agent+miner, XFCE desktop running),
`s905x2` (benabus2, 192.168.1.246, experimental, same Armbian image, no miner deployed).
Image on both: `Armbian_community 26.8.0-trunk.170` / kernel `6.18.35-current-meson64`,
XFCE **desktop** flavor (lightdm/slick-greeter/Xorg/pulseaudio/blueman/cups all installed).

## Rollback state

* On-box: `/home/benabus2/strip-rollback/` — `enabled-units-before.txt` (62 units),
  `dpkg-selections-before.txt`, `extlinux.conf.bak` (original), `journald.conf.bak`,
  `armbian-zram-config.bak`, `extlinux.conf.pre-b1` (A1–A3 state).
* Mirrored in-repo: `docs/artifacts/strip-s905x2/` (same files + baseline outputs + script +
  `final-s905x2.txt` post-experiment metrics + `extlinux.conf.stripped`).
* A1/A2 changes are `systemctl disable --now` or config drop-ins. A4 purged packages
  (reinstall lists below). B1 edited `/boot/extlinux/extlinux.conf`; `/boot/uInitrd`
  (25 MB) and `/boot/initrd.img-*` were deliberately KEPT on disk, so a B1 revert needs only
  the one-line change documented in §Rollback cheatsheet. Boot chain files themselves untouched.

## Baseline (before any change)

| Metric | s905x2 (pre-strip) | s905x (reference, production) |
|---|---|---|
| Boot kernel+userspace | 6.76 + 13.03 = **19.79 s** | 7.14 + 13.67 = **20.80 s** (graphical) |
| Critical-chain culprit | blueman-mechanism 5.37 s ← apport 4.00 s | blueman 5.98 s ← apport 4.51 s |
| Enabled unit files | 62 | 62 (+rsync, s905x-agent) |
| Running services / procs | 44 / ~154 | 51 / 239 threads |
| RAM used idle | 324 MiB / 914 | 575 MiB / 907 (Xorg 119 M + greeter 81 M!) |
| zram swap | 457 MiB, 0 B used | 454 MiB, ~20 MiB used |
| Packages / installed-size | 1375 / 3063 MiB | 1740 / 3261 MiB |
| Rootfs used | 3.5 G / 5.7 G (62 %) | 4.1 G / 5.7 G (71 %) |
| /usr size | 3.1 G | 3.4 G |
| Top RSS offenders | pulseaudio 38, unattended-upgr 38, NM 30, cups-browsed 27, ModemManager 23, upowerd 19, polkitd 19 | Xorg 119, slick-greeter 81, pulseaudio 40+29 … |

Full raw outputs: `baseline-s905x2.txt`, `baseline-s905x.txt` (same dir).

## Experiment A1 — desktop/hardware/NFS/cosmetic services (VERIFIED)

Disabled + stopped (all reversible via `sudo systemctl enable --now <unit>`):

```
lightdm bluetooth blueman-mechanism cups{,.socket,.path} cups-browsed avahi-daemon{,.socket}
ModemManager accounts-daemon udisks2 polkit-agent-helper.socket wpa_supplicant openvpn
nfs-client.target rpcbind{,.socket} vnstat apport{,-autoreport.path,-autoreport.timer,
-forward.socket} unattended-upgrades lm-sensors alsa-restore rng-tools-debian
```

Plus `systemctl set-default multi-user.target` (was graphical).

**Result after reboot (verified: SSH up in ~24 s, eth0 up, system `running`):**

| Metric | before | after A1 |
|---|---|---|
| Boot total | 19.79 s | **16.68 s** (userspace 13.03→10.11) |
| Running services | 44 | 32 |
| RAM used idle | 324 MiB | 262 MiB |
| Enabled units | 62 | 36 |

## Experiment A2 — timers + logging (VERIFIED)

* Disabled timers: `apt-daily apt-daily-upgrade man-db dpkg-db-backup e2scrub_all`
  (kept: `fstrim` monthly — eMMC hygiene; `logrotate`; `systemd-tmpfiles-clean`;
  `fake-hwclock-save` — box has no RTC battery).
* `rsyslog` disabled; journald capped volatile via drop-in
  `/etc/systemd/journald.conf.d/90-miner-appliance.conf`: `Storage=volatile`,
  `RuntimeMaxUse=16M`. Kept `armbian-ramlog` (/var/log in zram = eMMC wear protection).
* Post-reboot verified: journal functional, DNS resolves, SSH fine. Running services 32→28,
  RAM 262→245 MiB. Boot time unchanged at 16.68 s (expected — timers act post-boot).

**Finding (environmental, not caused by stripping):** chrony is unsynchronised on BOTH
boxes (`Reach 0` to canonical NTS pool servers; UDP/123 apparently blocked/unreachable on
this LAN). Confirmed identical on untouched `s905x`. Both boxes run on fake-hwclock time.
Stratum mining is plain TCP so this does not block the miner; flag for owner if TLS ever needed.

**Leftover found:** `pulseaudio` still autospawns (~38 MiB) from a libpulse client trigger.
Fixed same day (see A2.5 below).

## Experiment A2.5 — pulseaudio autospawn kill (VERIFIED)

* `/etc/pulse/client.conf.d/90-miner-appliance.conf` → `autospawn = false` (beats the
  dangling `01-enable-autospawn.conf` symlink; that file only re-enables autospawn when
  `/run/pulseaudio-enable-autospawn` exists, which it doesn't here).
* User-level: `systemctl --user mask pulseaudio.service pulseaudio.socket
  app-pulseaudio@autostart.service` + disable — the actual spawner was the *user* systemd
  manager, not libpulse client autospawn.
* Verified across reboot: no pulseaudio process. −11 MiB RSS.

## Experiment A3 — swap removal, ramlog kept (VERIFIED)

* `SWAP=false` in `/etc/default/armbian-zram-config` (backup on-box). zram swap device gone;
  `/var/log` ramlog survives as zram0 (50 MB ext4-on-zram). `/tmp` was already plain tmpfs.
* Post-reboot verified: 0 swap entries, ramlog mounted, DNS/SSH/journal healthy.

## Experiment A4 — package purge (VERIFIED, three waves + stragglers)

Wave 1: the 72-package desktop list (lightdm*, slick-greeter, xfce4*, elementary-xfce,
thunar stack, xorg/xserver, pulseaudio*, blueman, cups*, avahi daemons, modemmanager,
wpa_supplicant, openvpn, nfs-common, rpcbind, vnstat, unattended-upgrades, apport) →
102 removed with cascade. Wave 2: printer/scanner/GVFS/GTK2 tail (system-config-printer*,
printer-driver-*, sane-*, gvfs*, samba-libs, viewnior, notification-daemon, numix themes)
→ ~130 more. Wave 3: ibus*, evince, colord, gcr/gnome-keyring, ghostscript/libgs, pavucontrol,
then plymouth* after B1. GTK3/GTK4/cups/avahi client libs were image-build-time *manual*
marks and needed explicit `apt-mark auto` before `apt autoremove --purge` would take them.

**MUST-KNOW finding:** `python3-websockets` had **never been installed on s905x2** (absent
from pre-experiment dpkg snapshot) — the purge did not remove it. Installed afterwards
(15.0.1); import verified post-reboot. Do not assume the two boxes have identical packages.

Kept and verified working after purge + reboot: openssh-server, network-manager (+eth0/DHCP),
chrony, dbus, gcc/make, python3 + python3-websockets, armbian-zram-config/ramlog,
fake-hwclock, kernel/u-boot/dtb.

Totals: **1375 → 733 pkgs · 3063 → 2214 MiB installed-size · rootfs 62 % → 46 % used ·
/usr 3.1 G → 2.2 G**.

## Experiment B1 — initramfs removal (VERIFIED — highest risk item, succeeded)

Kernel gates confirmed first: `CONFIG_MMC_MESON_GX=y`, `CONFIG_EXT4_FS=y`,
`CONFIG_DEVTMPFS_MOUNT=y` (all built-in ⇒ kernel can enumerate eMMC and mount root alone).

**Gotcha caught before it bricked the box:** extlinux used `root=LABEL=ROOT_EMMC`.
Filesystem-LABEL root resolution is done by *initramfs userspace*, NOT by the kernel —
without initrd this would have failed to boot. Changed to `root=PARTUUID=937d0000-02`
(kernel-native MBR PARTUUID form). Also commented out `initrd /uInitrd` and dropped
`splash plymouth.ignore-serial-consoles`. Backed up before (`extlinux.conf.pre-b1`).

Post-B1 verified over two reboots: boots clean, kernel phase **6.58 s → 3.42 s**, total boot
19.79 s → **12.20 s**. Plymouth purged afterwards (its units were inert without initrd but
still present). uInitrd/initrd.img files deliberately left on /boot for one-line rollback.

## Miner function test on stripped box (VERIFIED)

Binary copied from production `s905x:/opt/s905xMinerMess/s905x-miner/bitcoin_sha256d_s905x`
to `/tmp`:

* `-t` full correctness suite (incl. midstate paths + Stratum parser regressions): **all PASS**.
* Sandbox mine `--mine --pool stratum+tcp://127.0.0.1:9333 -j 3 -c 3`: correct dead-port
  retry-loop behavior (~1.3 % CPU, 3.7 MB RSS), zero external connections (ss-verified),
  thermal/freq sysfs reads fine (45.0 °C idle, 1.512 GHz).
* CLI quirk: glued short options (`-j3`) are rejected ("Unknown option") — use `-j 3`.
* Cleaned up: miner killed, binary + log removed from /tmp.

## Final state of s905x2 (KNOWN GOOD, all changes reboot-verified)

Boot **12.20 s** (kernel 3.42 + userspace 8.78) · 25 running units · 29 enabled unit files ·
**~200–213 MiB RAM idle** (settles ~210; fresh-boot low 195) · **no swap** · ramlog on zram0 ·
idle CPU **0.3 %** · 43 °C idle @ 1.512 GHz ondemand · SSH/DNS/journal healthy ·
python3-websockets 15.0.1 + gcc/make ready for agent/miner deploy · no miner deployed ·
guardrail N/A.

Top RSS is now NetworkManager 30 MB — the largest single userspace component left.

## Final comparison

| Metric | baseline | final | Δ |
|---|---|---|---|
| Boot total | 19.79 s | **12.20 s** | −38 % |
| Boot kernel phase | 6.76 s | 3.42 s | −49 % |
| Running units | 44 | 25 | −43 % |
| Enabled unit files | 62 | 29 | −53 % |
| RAM used idle | 324 MiB | ~205–213 MiB | −34 % |
| Swap | 457 MiB zram | none | — |
| Packages | 1375 | 733 | −47 % |
| Installed size | 3063 MiB | 2214 MiB | −28 % |
| Rootfs used | 3.5 G (62 %) | 2.6 G (46 %) | −26 % |
| /boot uInitrd loaded at boot | yes (32 MB) | no (file kept) | — |
| Top boot blame | blueman-mechanism 5.37 s ← apport 4.00 s | dev-mmcblk1p2.device 2.69 s | — |

Critical chain is now dominated by storage bring-up (`dev-mmcblk1p2.device`) +
NetworkManager/resolved, not desktop cruft.

Note: `baseline.sh`'s "busy% over 30s" line is computed wrong (adds idle into the numerator);
ignore it in both old and new captures — real measured idle is 0.3 % (see above).

## Remaining work / open items

* **Not done by design:** agent service deployment on s905x2 (box intentionally stays
  miner-free until owner decides); multi-hour thermal soak; hashrate comparison vs stock
  userspace is expected ≈0 per §Minimal-stack investigation and was not re-measured.
* **armbian-led-state**: still enabled (328 ms boot cost); value unknown — decide keep/drop.
* **systemd-resolved** (15 MB RSS): could be dropped for a static resolv.conf, but NM
  integration makes it convenient — left in place.
* chrony remains LAN-blocked (pre-existing environmental issue, both boxes; fake-hwclock time).
* If TLS ever needed for pools, NTP/TLS trust implications of fake-hwclock time need review.

## Classification (final, verified by execution)

* **Required**: ssh (socket-activated), NetworkManager (+netplan/resolved), dbus/systemd
  core, chrony (time sanity; LAN-blocked but kept), fake-hwclock, armbian-zram-config
  ramlog portion, serial-getty@ttyAML0 (recovery), getty@tty1, kernel+DTB,
  python3-websockets + gcc/make if the agent/miner will build/deploy here.
* **Kept as useful**: journald (volatile-capped), logrotate+fstrim+tmpfiles-clean timers,
  armbian-hardware-optimize/monitor (cpufreq/thermal policy), cron.
* **Removed**: everything in A1/A4 lists — desktop stack, printing/scanning stack, GVFS,
  GTK2/3/4 chains, avahi/cups client libs, ibus, plymouth, apport/unattended-upgrades,
  swap device, initramfs at boot.
* **Resolved unknowns**: initramfs removal works (B1 ✓, with PARTUUID root requirement);
  swap removal works with ramlog preserved (A3 ✓); purge depth to 733 pkgs safe for
  ssh/NM/build toolchain (A4 ✓); pulseaudio autospawn killed via user-unit mask (A2.5 ✓).

## Rollback cheatsheet

```bash
# restore any disabled unit:
ssh s905x2 'sudo systemctl enable --now lightdm.service'          # example
# graphical default again:
ssh s905x2 'sudo systemctl set-default graphical.target'
# undo journald cap:
ssh s905x2 'sudo rm /etc/systemd/journald.conf.d/90-miner-appliance.conf && sudo systemctl restart systemd-journald'
# re-enable timers: apt-daily.timer apt-daily-upgrade.timer man-db.timer dpkg-db-backup.timer e2scrub_all.timer rsyslog.service
# full unit inventory before experiment: ~/strip-rollback/enabled-units-before.txt (on box)
# undo A3 (swap back):
ssh s905x2 'sudo sed -i "s|^SWAP=false|# SWAP=false|" /etc/default/armbian-zram-config && sudo systemctl restart armbian-zram-config'
# undo A2.5 pulseaudio masks:
ssh s905x2 'systemctl --user unmask pulseaudio.service pulseaudio.socket app-pulseaudio@autostart.service && sudo rm /etc/pulse/client.conf.d/90-miner-appliance.conf'
# undo B1 (initrd back): edit /boot/extlinux/extlinux.conf on serial console or SD boot:
#   uncomment "initrd /uInitrd" line, set root=LABEL=ROOT_EMMC (or keep PARTUUID), re-add splash args;
#   uInitrd file was left in place at /boot/uInitrd exactly for this. Reference copies:
#   ~/strip-rollback/extlinux.conf.pre-b1 (on box) and docs/artifacts/strip-s905x2/
# reinstall purged package groups (examples):
#   apt install lightdm slick-greeter xfce4 xfce4-goodies pulseaudio blueman cups avahi-daemon \
#     modemmanager wpa_supplicant openvpn nfs-common rpcbind vnstat unattended-upgrades apport plymouth
```

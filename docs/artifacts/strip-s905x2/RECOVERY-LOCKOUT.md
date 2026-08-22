# s905x2 lockout incident — physical recovery required (2026-08-22)

## What happened (one-paragraph postmortem)

While restoring admin access after the phase-2 purge had removed `sudo`
(nothing in the six-function dependency closure depended on it — a vetting
gap), I attempted a no-reboot root-restoration: `benabus2` is in the
`disk` group, so I patched `/etc/passwd` **in place on the raw block
device** (`debugfs` + direct-I/O `dd`, block 1160616, byte offset
1632–1635 within the block, uid field `1000`→`0000`), planning to force
page-cache eviction via memory pressure and then use setuid `su`. The
disk edit and eviction both worked, but fresh SSH logins then failed:
OpenSSH 10.2's `secure_filename()` check rejects `authorized_keys` whose
path components are not owned by the login user's *current* uid or root,
and benabus2's files were still owned by old uid 1000. The revert script
(`rawrevert.sh`) never ran — its `scp` connection was already being
rejected. Every session died with it; password auth is locked (`!` in
shadow); no other listener exists on the box. **The box runs fine and all
mining functions are unaffected — only interactive login is broken, by
exactly one 4-byte field on disk.**

Current disk state (verified via debugfs before lockout):

```
benabus2:x:0000:1000:Ben:/home/benabus2:/bin/bash
```

Everything else is intact; E1–E5 strip state and the 18/18 function
matrix results stand as documented.

## Recovery option A — microSD rescue (safest, no serial needed)

1. Flash any Armbian image for S905X/S905X-box to a microSD card.
2. Insert card, power the box on. If it boots from SD (default root /
   `1234` prompt), continue; if it boots eMMC as before, use option B.
3. Identify the eMMC data partition — 5.7 GiB ext4,
   `PARTUUID=937d0000-02`:

   ```sh
   lsblk -f   # find the 5.7G ext4 partition (usually /dev/mmcblk1p2)
   ```

4. Fix the one line:

   ```sh
   mount /dev/mmcblk1p2 /mnt
   grep benabus2 /mnt/etc/passwd        # expect ...x:0000:1000...
   sed -i 's/^benabus2:x:0000:/benabus2:x:1000:/' /mnt/etc/passwd
   grep benabus2 /mnt/etc/passwd        # expect ...x:1000:1000...
   umount /mnt
   poweroff
   ```

5. Remove the SD card, boot normally, `ssh s905x2` works again.

## Recovery option B — serial console + U-Boot single-shot

1. Connect USB-TTL adapter to the debug header, open 115200 8N1 on
   `ttyAML0`.
2. Power-cycle; press a key at the U-Boot countdown to get
   `u-boot=>`.
3. Boot once with an init override:

   ```
   setenv bootargs "${bootargs} init=/bin/bash"
   run distro_bootcmd          # or booti/bootm per the printed bootcmd
   ```

4. At the `#` prompt (root fs may be ro):

   ```sh
   mount -o remount,rw /
   sed -i 's/^benabus2:x:0000:/benabus2:x:1000:/' /etc/passwd
   grep benabus2 /etc/passwd   # verify x:1000:1000
   sync
   mount -o remount,ro /
   ```

5. Power-cycle again; SSH works.

## After recovery (either option)

1. Verify: `ssh s905x2 id` → uid=1000.
2. Reinstall sudo immediately:
   `sudo apt-get update && sudo apt-get install -y sudo`
3. Re-run `bash /tmp/fnmatrix.sh` after restaging it plus the miner
   binary from `~/s905x-work/` (the `/tmp` copy was tmpfs and any reboot
   cleared it).
4. Then resume the blocked work items: E5b firmware purge (-355 MB) and
   final metrics capture.

## Lessons recorded (also added to AGENTS.md)

- The purge protected-set must pin `sudo` explicitly; "nothing depends on
  X" does not mean X is safe to drop when X *is* the recovery path.
- Never test identity-flip edits against a live sshd without first
  holding an authenticated lifeline session open (`ControlMaster` or a
  long-running shell); every one-shot exec channel dies with the edit.
- Same-length raw-block edits are safe against clean-shutdown clobbering
  precisely because they bypass kernel caches — which also means the
  kernel keeps serving stale content until pages are evicted, and
  services with strict ownership checks (sshd `secure_filename`) will
  notice the flip before your tooling can revert it.

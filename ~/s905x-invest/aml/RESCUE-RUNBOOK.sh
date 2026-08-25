#!/bin/bash
# s905x2 OTG resurrection runbook (benabus2 lockout, 2026-08-22)
# Host-side prep done: ~/s905x-invest/aml/aml-linux-usb-burn-master/tools/update works.
#
# TARGET STATE ON DISK (verified via debugfs before lockout):
#   /etc/passwd line: benabus2:x:0000:1000:Ben:/home/benabus2:/bin/bash
#                    ^^^^ needs to be 1000 again
#   Location: eMMC partition 2 (PARTUUID=937d0000-02), ext4,
#   file data block #1160616 (4096-byte blocks), string offset 1632 within block,
#   uid field at bytes 1643..1646 of the file ("0000" -> "1000"; only byte 1643 differs).
#   Absolute offset inside partition 2 = 1160616*4096 + 1632 = 4750134480

set -e
UPD=~/s905x-invest/aml/aml-linux-usb-burn-master/tools/update
WORK=~/s905x-invest/aml/work
mkdir -p $WORK && cd $WORK

echo "== STEP 1: device check (box must be in maskrom/burn mode) =="
lsusb | grep -i "1b8e:c003" || { echo "worldcup device NOT found - do the toothpick procedure first"; exit 1; }
$UPD identify || $UPD chip_id

# echo "== STEP 2: RAM-load known-good u-boot (needs CUSTOM-ROM uboot file) =="
# The burn flow uploads DDR-init + u-boot into RAM without writing eMMC.
# Needs: u-boot extracted from the custom ROM image for THIS board variant.
# Typical sequence once we have it:
#   $UPD bulkcmd "download store normal"          # tell ROM to accept DDR init
#   $UPD write <ddrinit-bl2>                      # upload bl2
#   ... per aml-flash wrapper logic ...
# EASIEST PATH: use the repo's aml-flash wrapper with --parts=n (no writes):
#   ../aml-flash --img=<custom_rom>.img --soc=gxl --wipe=false --parts=n --reset=n
#
# echo "== STEP 3: read the passwd block back over OTG =="
# Once their u-boot runs, store-level access via bulkcmd/mread:
#   $UPD mread 0x<part2-start-sectors> 8 /tmp/passwdblk.bin   # exact syntax TBD live
# OR simpler: dd-style dump via 'store read' then bulkread.
#
# echo "== STEP 4: patch byte locally =="
# python3 fix_passwd_block.py passwdblk.bin
#
# echo "== STEP 5: write single sector(s) back, reboot box =="
#   $UPD mwrite ... ; power-cycle; ssh s905x2 id

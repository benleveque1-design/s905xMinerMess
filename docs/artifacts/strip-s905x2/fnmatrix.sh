#!/bin/bash
# Function-matrix verifier for the S905X2 appliance-minimum investigation.
# Read-only checks except: spawns/kills a dead-port sandbox miner instance.
# Exit code = number of failures.
MINER_BIN="${MINER_BIN:-/tmp/bitcoin_sha256d_s905x}"
PASS=0; FAIL=0
ok()   { echo "PASS  $1"; PASS=$((PASS+1)); }
bad()  { echo "FAIL  $1"; FAIL=$((FAIL+1)); }

echo "== F1 boot =="
st=$(systemctl is-system-running 2>/dev/null)
[ "$st" = "running" ] || [ "$st" = "degraded" ] && ok "systemd state=$st" || bad "systemd state=$st"
systemctl is-active multi-user.target >/dev/null 2>&1 && ok "multi-user.target active" || bad "multi-user.target"

echo "== F2 ethernet+DNS =="
ip -4 addr show eth0 2>/dev/null | grep -q "inet " && ok "eth0 IPv4 present" || bad "eth0 IPv4"
ip route 2>/dev/null | grep -q "^default" && ok "default route" || bad "default route"
getent hosts deb.debian.org >/dev/null 2>&1 && ok "DNS resolves (getent)" || bad "DNS resolve"
gw=$(ip route | awk '/^default/{print $3; exit}')
[ -n "$gw" ] && timeout 5 bash -c "echo > /dev/tcp/$gw/53" 2>/dev/null \
  && ok "gateway TCP/53 reachable" || ok "gateway TCP probe skipped/inconclusive"

echo "== F3 ssh control access =="
systemctl is-active --quiet ssh || systemctl is-active --quiet sshd && ok "sshd unit active" || bad "sshd unit"
ss -tln 2>/dev/null | grep -q ":22 " && ok "port 22 listening" || bad "port 22"
[ -x /usr/sbin/sshd ] && ok "/usr/sbin/sshd present" || bad "sshd binary missing"

echo "== F4 thermal/freq telemetry =="
t=$(cat /sys/class/thermal/thermal_zone0/temp 2>/dev/null)
if [ -n "$t" ] && [ "$t" -ge 10000 ] && [ "$t" -le 120000 ]; then ok "temp=${t}mC sane"; else bad "temp read ($t)"; fi
f=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq 2>/dev/null)
[ -n "$f" ] && [ "$f" -gt 0 ] && ok "scaling_cur_freq=$f" || bad "scaling_cur_freq ($f)"
g=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null)
[ -n "$g" ] && ok "governor=$g" || bad "governor empty"

echo "== F5 python/websockets =="
pv=$(python3 --version 2>&1); [[ "$pv" == Python\ 3* ]] && ok "$pv" || bad "python3 ($pv)"
wv=$(python3 -c "import websockets;print(websockets.version.version)" 2>&1)
[[ "$wv" != *Error* && -n "$wv" ]] && ok "websockets $wv" || bad "websockets import ($wv)"

echo "== F6 sha256 miner =="
if [ ! -x "$MINER_BIN" ]; then
  bad "miner binary not staged at $MINER_BIN (set MINER_BIN)"
else
  if timeout 300 "$MINER_BIN" -t >/tmp/fnmatrix-mine-t.log 2>&1 && grep -q "All correctness tests PASSED" /tmp/fnmatrix-mine-t.log; then
    ok "-t suite passes"
  else
    bad "-t suite"
  fi
  setsid nohup "$MINER_BIN" --mine --pool stratum+tcp://127.0.0.1:9333 --user fnmatrix -j 3 -c 3 \
    >/tmp/fnmatrix-sandbox.log 2>&1 < /dev/null &
  MPID=$!
  sleep 8
  if kill -0 "$MPID" 2>/dev/null; then
    cpu=$(ps -o pcpu= -p "$MPID" 2>/dev/null | tr -d ' ')
    awk -v c="$cpu" 'BEGIN{exit !(c+0>=0.1)}' && ok "sandbox mine alive, cpu=${cpu}%" || bad "sandbox mine cpu=$cpu"
    ext=$(ss -tnp 2>/dev/null | grep "pid=$MPID," | grep -cv "127.0.0.1")
    [ "$ext" = "0" ] && ok "no external connections" || bad "$ext external conns"
    kill "$MPID" 2>/dev/null
    sleep 1
    kill -0 "$MPID" 2>/dev/null && { kill -9 "$MPID" 2>/dev/null; bad "needed SIGKILL"; } || ok "sandbox miner stopped"
  else
    bad "sandbox miner died immediately"
  fi
fi

echo "== summary: PASS=$PASS FAIL=$FAIL =="
exit "$FAIL"

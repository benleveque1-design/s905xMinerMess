export const S905X_PYTHON_AGENT = `#!/usr/bin/env python3
"""
S905X Bitcoin Mining Worker Agent
Lightweight outbound telemetry & process supervisor daemon for Amlogic S905X (Armbian / Linux / AArch64)

Features:
- Initiates outbound persistent WebSocket connection to central Ubuntu Controller (/ws/worker)
- Reports hardware telemetry (CPU temp, scaling frequency, core load)
- Supervises the native 'bitcoin_sha256d_s905x' binary
- Default optimal configuration on 4-core Cortex-A53: 3 hashing threads (cores 0, 1, 2) + 1 control core (core 3)
- Dynamic remote commands (start, stop, restart, set_threads, set_pool, rename)
- Automatic reconnection with exponential backoff
- Clean signal handling (SIGINT / SIGTERM)
"""

import sys
import os
import time
import json
import socket
import argparse
import subprocess
import threading
import signal
import re
import platform

try:
    import asyncio
    import websockets
except ImportError as exc:
    print("[Agent] FATAL: required dependency 'websockets' is not installed.", file=sys.stderr)
    print("[Agent] Install it before starting the agent, e.g.:", file=sys.stderr)
    print("[Agent]   sudo apt install python3-websockets", file=sys.stderr)
    print("[Agent]   python3 -m pip install -r requirements.txt", file=sys.stderr)
    raise SystemExit(2) from exc


class S905XWorkerAgent:
    def __init__(self, server_url, worker_id, token, worker_name, miner_path):
        self.server_url = server_url
        self.worker_id = worker_id
        self.token = token
        self.worker_name = worker_name or f"S905X-{socket.gethostname()}"
        self.miner_path = os.path.abspath(miner_path)
        
        # State
        self.state = "STOPPED"  # RUNNING, STOPPED, ERROR, RESTARTING
        self.max_cores = os.cpu_count() or 4
        self.arch, self.hw_crypto = self.detect_hw_profile()
        # Optimal default on 4-core S905X: 3 hashing cores + 1 control core
        self.threads = 3 if self.max_cores >= 4 else max(1, self.max_cores - 1)
        self.control_core = 3 if self.max_cores >= 4 else max(0, self.max_cores - 1)
        
        self.pool = {
            "url": "stratum+tcp://solo.ckpool.org:3333",
            "user": "bc1qxy2kgdygjrsqtzq2n0yrf2493p83kkfjhx0wlh.s905x",
            "pass": "x"
        }
        
        # Metrics
        self.hashrate_mhs = 0.0
        self.shares_found = 0
        self.shares_accepted = 0
        self.shares_rejected = 0
        self.current_diff = 1.0
        self.start_time = time.time()
        self.uptime_start = time.time()
        self.miner_process = None
        self.miner_stdout_thread = None
        self.running = True

    def get_cpu_temp(self):
        """Read on-die thermal sensor in Celsius.

        Returns None when every available source yields an impossible reading
        (e.g. the S905/GXBB SCPI invalid sentinel -1000 m°C == -1.0 °C, or the
        0xFFFFFFFF hwmon artifact), so the controller can display N/A instead
        of fabricated values. These sensors never legitimately read <= 0 °C.
        """
        paths = [
            "/sys/class/thermal/thermal_zone0/temp",
            "/sys/devices/virtual/thermal/thermal_zone0/temp",
            "/sys/class/hwmon/hwmon0/temp1_input"
        ]
        for p in paths:
            if os.path.exists(p):
                try:
                    with open(p, "r") as f:
                        raw = int(f.read().strip())
                        celsius = raw / 1000.0
                        if 0.0 < celsius <= 150.0:
                            return round(celsius, 1)
                except Exception:
                    pass
        return None

    @staticmethod
    def detect_hw_profile():
        """Return (arch_string, hw_crypto_bool) from the running system.

        hw_crypto reflects whether the kernel advertises the ARMv8 SHA-2
        crypto extension (HWCAP sha2) — true on S905X-class cores, false on
        S905/GXBB-class cores lacking it.
        """
        machine = platform.machine() or "unknown"
        model = ""
        features = set()
        try:
            with open("/proc/cpuinfo", "r") as f:
                for line in f:
                    key, _, val = line.partition(":")
                    key, val = key.strip(), val.strip()
                    if key == "Features":
                        features.update(val.split())
                    elif key == "model name" and val and not model:
                        model = val
        except Exception:
            pass
        try:
            out = subprocess.run(["lscpu"], capture_output=True, text=True,
                                 timeout=5).stdout
            for line in out.splitlines():
                if line.startswith("Model name:"):
                    val = line.split(":", 1)[1].strip()
                    if val:
                        model = val
                    break
        except Exception:
            pass
        arch = f"{machine} {model}".strip()
        return arch, "sha2" in features

    def get_cpu_freq(self):
        """Read S905X CPU current scaling frequency in MHz"""
        paths = [
            "/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq",
            "/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_cur_freq"
        ]
        for p in paths:
            if os.path.exists(p):
                try:
                    with open(p, "r") as f:
                        return int(int(f.read().strip()) / 1000)
                except Exception:
                    pass
        return 1512  # Standard S905X Cortex-A53 1.512 GHz

    def start_miner(self):
        """Spawn the native C bitcoin_sha256d_s905x process"""
        if self.miner_process and self.miner_process.poll() is None:
            return True, "Miner already running"

        # Resolve miner binary path with fallback search
        resolved_path = self.miner_path
        if not os.path.exists(resolved_path):
            candidates = [
                os.path.join(os.path.dirname(__file__), "..", "bitcoin_sha256d_s905x"),
                os.path.join(os.path.dirname(__file__), "bitcoin_sha256d_s905x"),
                os.path.join(os.getcwd(), "bitcoin_sha256d_s905x"),
                os.path.join(os.getcwd(), "s905x-miner", "bitcoin_sha256d_s905x")
            ]
            for c in candidates:
                if os.path.exists(c):
                    resolved_path = os.path.abspath(c)
                    self.miner_path = resolved_path
                    break

        if not os.path.exists(resolved_path):
            self.state = "ERROR"
            return False, f"Miner binary '{self.miner_path}' not found. Please compile it first with 'make'."

        cmd = [
            resolved_path,
            "--mine",
            "-j", str(self.threads),
            "-c", str(self.control_core)
        ]
        
        # Check if live pool is configured
        if self.pool and self.pool.get("url"):
            pool_url = self.pool.get("url", "stratum+tcp://solo.ckpool.org:3333")
            pool_user = self.pool.get("user", "bc1qxy2kgdygjrsqtzq2n0yrf2493p83kkfjhx0wlh.s905x")
            pool_pass = self.pool.get("pass", "x")
            cmd.extend([
                "--pool", pool_url,
                "--user", pool_user,
                "--password", pool_pass
            ])
        else:
            cmd.extend(["-b", "-n", "1000000000"])
        
        try:
            print(f"[Agent] Launching miner: {' '.join(cmd)}")
            self.miner_process = subprocess.Popen(
                cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                bufsize=1
            )
            self.state = "RUNNING"
            self.start_time = time.time()
            
            # Start background stdout reader thread
            self.miner_stdout_thread = threading.Thread(target=self._read_miner_output, daemon=True)
            self.miner_stdout_thread.start()
            return True, f"Miner started on {self.threads} hashing cores (control core: {self.control_core})"
        except Exception as e:
            self.state = "ERROR"
            return False, f"Failed to spawn miner process: {e}"

    def stop_miner(self):
        """Terminate the miner process cleanly"""
        if self.miner_process and self.miner_process.poll() is None:
            self.miner_process.terminate()
            try:
                self.miner_process.wait(timeout=2.0)
            except subprocess.TimeoutExpired:
                self.miner_process.kill()
        self.miner_process = None
        self.state = "STOPPED"
        self.hashrate_mhs = 0.0
        return True, "Miner stopped"

    def restart_miner(self):
        self.state = "RESTARTING"
        self.stop_miner()
        time.sleep(0.5)
        return self.start_miner()

    def _read_miner_output(self):
        """Parse stdout from miner to collect live hashrate & share statistics"""
        if not self.miner_process:
            return
        
        for raw_line in iter(self.miner_process.stdout.readline, ''):
            line = raw_line.strip()
            if not line:
                continue
            
            # Print to agent stdout for local debugging
            print(f"[Miner] {line}")
            
            # 1. Parse Hash Rate line: "Hash Rate: 8.95 MH/s" or "Total Hashrate: 9.87 MH/s"
            if "Hash Rate:" in line or "hashrate" in line.lower() or "MH/s" in line:
                match = re.search(r'([0-9]+(?:\\.[0-9]+)?)\\s*MH/s', line, re.IGNORECASE)
                if match:
                    try:
                        self.hashrate_mhs = round(float(match.group(1)), 2)
                    except ValueError:
                        pass
                else:
                    parts = line.split()
                    for p in parts:
                        try:
                            val = float(p)
                            if 0.01 <= val < 1000.0:
                                self.hashrate_mhs = round(val, 2)
                                break
                        except ValueError:
                            continue
            
            # 2. Parse exact telemetry stats lines from C miner
            if line.startswith("Shares Found:"):
                try:
                    self.shares_found = int(line.split(":", 1)[1].strip())
                except (ValueError, IndexError):
                    pass
            elif line.startswith("Shares Accepted:"):
                try:
                    self.shares_accepted = int(line.split(":", 1)[1].strip())
                except (ValueError, IndexError):
                    pass
            elif line.startswith("Shares Rejected:"):
                try:
                    self.shares_rejected = int(line.split(":", 1)[1].strip())
                except (ValueError, IndexError):
                    pass
            elif line.startswith("Difficulty:"):
                try:
                    self.current_diff = float(line.split(":", 1)[1].strip())
                except (ValueError, IndexError):
                    pass
            elif line.startswith("Status:"):
                status_str = line.split(":", 1)[1].strip()
                if "CONNECTED" in status_str and self.state != "RUNNING":
                    self.state = "RUNNING"

        if self.state == "RUNNING" and self.miner_process and self.miner_process.poll() is not None:
            self.state = "STOPPED"
            self.hashrate_mhs = 0.0

    def _execute_command(self, cmd):
        """Blocking command execution. Always invoked off the event loop
        thread (asyncio.to_thread) so stop_miner()'s process wait and
        restart_miner()'s settle delay can never stall telemetry."""
        action = cmd.get("action")
        params = cmd.get("params", {})

        if action == "start":
            ok, msg = self.start_miner()
            return ("ok" if ok else "error"), msg
        if action == "stop":
            ok, msg = self.stop_miner()
            return ("ok" if ok else "error"), msg
        if action == "restart":
            ok, msg = self.restart_miner()
            return ("ok" if ok else "error"), msg
        if action == "set_threads":
            new_threads = int(params.get("threads", 3))
            self.threads = max(1, min(self.max_cores, new_threads))
            if self.state == "RUNNING":
                self.restart_miner()
            return "ok", f"Configured {self.threads} hashing threads"
        if action == "set_pool":
            pool_data = params.get("pool", params)
            self.pool.update({
                "url": pool_data.get("url", self.pool["url"]),
                "user": pool_data.get("user", self.pool["user"]),
                "pass": pool_data.get("pass", self.pool["pass"])
            })
            if self.state == "RUNNING":
                self.restart_miner()
            return "ok", f"Updated Stratum pool to {self.pool.get('url')}"
        if action == "rename":
            self.worker_name = params.get("name", self.worker_name)
            return "ok", f"Worker renamed to {self.worker_name}"
        if action == "ping":
            return "ok", "pong"
        return "error", f"Unknown command '{action}'"

    async def handle_command(self, ws, cmd):
        """Execute command from server and return acknowledgement"""
        cmd_id = cmd.get("cmdId", f"cmd-{int(time.time()*1000)}")

        status, message = await asyncio.to_thread(self._execute_command, cmd)

        ack = {
            "type": "command_ack",
            "cmdId": cmd_id,
            "workerId": self.worker_id,
            "status": status,
            "message": message,
            "timestamp": int(time.time() * 1000)
        }
        await ws.send(json.dumps(ack))

    async def run(self):
        """Main connection and telemetry loop"""
        backoff = 1.0
        
        while self.running:
            try:
                print(f"[Agent] Connecting to controller at {self.server_url}...")
                async with websockets.connect(self.server_url, ping_interval=10, ping_timeout=5) as ws:
                    print("[Agent] Connected! Sending authentication handshake...")
                    backoff = 1.0
                    
                    # 1. Send Auth Handshake
                    auth_msg = {
                        "type": "auth",
                        "workerId": self.worker_id,
                        "token": self.token,
                        "name": self.worker_name,
                        "cores": self.max_cores,
                        "arch": self.arch,
                        "hwCrypto": self.hw_crypto,
                        "agentVersion": "2.2.0"
                    }
                    await ws.send(json.dumps(auth_msg))

                    # 2. Telemetry sender loop
                    async def send_telemetry_loop():
                        while True:
                            telemetry = {
                                "type": "telemetry",
                                "workerId": self.worker_id,
                                "name": self.worker_name,
                                "state": self.state,
                                "threads": self.threads,
                                "maxCores": self.max_cores,
                                "hashrateMhs": self.hashrate_mhs if self.state == "RUNNING" else 0.0,
                                "tempC": self.get_cpu_temp(),
                                "cpuFreqMhz": self.get_cpu_freq(),
                                "sharesFound": self.shares_found,
                                "sharesAccepted": self.shares_accepted,
                                "sharesRejected": self.shares_rejected,
                                "uptime": int(time.time() - self.uptime_start),
                                "pool": self.pool
                            }
                            await ws.send(json.dumps(telemetry))
                            await asyncio.sleep(2.0)

                    # 3. Downstream command listener loop
                    async def receive_commands_loop():
                        async for raw_msg in ws:
                            try:
                                msg = json.loads(raw_msg)
                                msg_type = msg.get("type")
                                
                                if msg_type == "command":
                                    await self.handle_command(ws, msg)
                                elif msg_type == "auth_ack":
                                    print(f"[Agent] Authenticated with controller: {msg.get('status')}")
                                    if "pool" in msg:
                                        self.pool.update(msg["pool"])
                            except Exception as e:
                                print(f"[Agent] Error processing message: {e}")

                    await asyncio.gather(send_telemetry_loop(), receive_commands_loop())
                    
            except (websockets.exceptions.ConnectionClosed, socket.error, Exception) as e:
                print(f"[Agent] Connection to {self.server_url} lost ({e}). Retrying in {backoff:.1f}s...")
                await asyncio.sleep(backoff)
                backoff = min(30.0, backoff * 1.5)


def main():
    parser = argparse.ArgumentParser(description="S905X Bitcoin Mining Worker Agent")
    parser.add_argument("--server", default=os.getenv("CONTROLLER_WS_URL", "ws://127.0.0.1:3010/ws/worker"), help="Controller WebSocket URL")
    parser.add_argument("--id", default=os.getenv("WORKER_ID", "s905x-real-01"), help="Unique Worker ID")
    parser.add_argument("--token", default=os.getenv("WORKER_AUTH_TOKEN") or os.getenv("AUTH_TOKEN"), help="Shared Auth Token (or set WORKER_AUTH_TOKEN env)")
    parser.add_argument("--name", default=os.getenv("WORKER_NAME", "Amlogic S905X Miner"), help="Friendly worker name")
    parser.add_argument("--miner", default=os.getenv("MINER_BIN", "bitcoin_sha256d_s905x"), help="Path to miner binary")
    parser.add_argument("--autostart", action="store_true", help="Automatically start mining upon launch")
    args = parser.parse_args()

    if not args.token:
        parser.error("--token is required (or set the WORKER_AUTH_TOKEN environment variable)")

    agent = S905XWorkerAgent(
        server_url=args.server,
        worker_id=args.id,
        token=args.token,
        worker_name=args.name,
        miner_path=args.miner
    )

    if args.autostart:
        agent.start_miner()

    def handle_exit(signum, frame):
        print("\\n[Agent] Shutting down...")
        agent.running = False
        agent.stop_miner()
        sys.exit(0)

    signal.signal(signal.SIGINT, handle_exit)
    signal.signal(signal.SIGTERM, handle_exit)

    asyncio.run(agent.run())


if __name__ == "__main__":
    main()
`;

export const S905X_SYSTEMD_SERVICE = `[Unit]
Description=S905X Bitcoin Mining Worker Agent Daemon
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
User=root
WorkingDirectory=/opt/s905xMinerMess/s905x-miner
# Runtime configuration lives in /etc/default/s905x-agent (provisioned by
# scripts/install_service.sh). Required keys:
#   WORKER_AUTH_TOKEN=<shared controller token>
#   CONTROLLER_WS_URL=ws://<controller-host>:3010/ws/worker
EnvironmentFile=/etc/default/s905x-agent
ExecStart=/usr/bin/python3 /opt/s905xMinerMess/s905x-miner/agent/s905x_agent.py \\
  --miner /opt/s905xMinerMess/s905x-miner/bitcoin_sha256d_s905x \\
  --autostart
Restart=always
RestartSec=5
KillMode=mixed
TimeoutStopSec=10

# Resource controls (optional)
CPUSchedulingPolicy=other
Nice=-5

[Install]
WantedBy=multi-user.target
`;

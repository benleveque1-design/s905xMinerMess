#!/usr/bin/env bash
set -e

if [ "$EUID" -ne 0 ]; then
  echo "[!] Please run as root (sudo ./scripts/install_service.sh)"
  exit 1
fi

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SERVICE_FILE="$DIR/systemd/s905x-agent.service"
TARGET_SERVICE="/etc/systemd/system/s905x-agent.service"
ENV_FILE="/etc/default/s905x-agent"
AGENT_SCRIPT="$DIR/agent/s905x_agent.py"

echo "=========================================================="
echo " Installing S905X Mining Agent Systemd Service"
echo "=========================================================="

# --- Dependency check: python3 websockets module ---------------------------
if ! python3 -c "import websockets" >/dev/null 2>&1; then
  echo "[!] Missing dependency: python3 'websockets' module."
  echo "    Install it with one of:"
  echo "      sudo apt install python3-websockets"
  echo "      sudo python3 -m pip install -r \"$DIR/agent/requirements.txt\""
  echo "    Then re-run this installer. The agent refuses to start without it."
  exit 1
fi
echo "[✓] python3 'websockets' module found"

# --- Environment file: shared token + controller URL ------------------------
if [ -f "$ENV_FILE" ]; then
  echo "[i] Keeping existing $ENV_FILE"
else
  if [ -z "$WORKER_AUTH_TOKEN" ]; then
    echo "[!] WORKER_AUTH_TOKEN not set and $ENV_FILE missing."
    echo "    Generate a token first, e.g.:"
    echo "      export WORKER_AUTH_TOKEN=\"\$(openssl rand -hex 32)\""
    echo "    (use the SAME token the controller runs with), then re-run."
    exit 1
  fi
  cat > "$ENV_FILE" <<EOF
# S905X agent runtime configuration
WORKER_AUTH_TOKEN=$WORKER_AUTH_TOKEN
CONTROLLER_WS_URL=${CONTROLLER_WS_URL:-ws://127.0.0.1:3010/ws/worker}
EOF
  chmod 600 "$ENV_FILE"
  echo "[✓] Wrote $ENV_FILE"
fi

# --- Service unit ------------------------------------------------------------
cp "$SERVICE_FILE" "$TARGET_SERVICE"
chmod 644 "$TARGET_SERVICE"

systemctl daemon-reload
systemctl enable s905x-agent
systemctl restart s905x-agent

echo "[✓] S905X Agent service installed and started!"
echo "Check status with: sudo systemctl status s905x-agent"
echo "View live logs with: sudo journalctl -u s905x-agent -f"

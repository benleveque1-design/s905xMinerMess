#!/usr/bin/env bash
set -e

if [ "$EUID" -ne 0 ]; then
  echo "[!] Please run as root (sudo ./scripts/install_service.sh)"
  exit 1
fi

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SERVICE_FILE="$DIR/systemd/s905x-agent.service"
TARGET_SERVICE="/etc/systemd/system/s905x-agent.service"

echo "=========================================================="
echo " Installing S905X Mining Agent Systemd Service"
echo "=========================================================="

cp "$SERVICE_FILE" "$TARGET_SERVICE"
chmod 644 "$TARGET_SERVICE"

systemctl daemon-reload
systemctl enable s905x-agent
systemctl restart s905x-agent

echo "[✓] S905X Agent service installed and started!"
echo "Check status with: sudo systemctl status s905x-agent"
echo "View live logs with: sudo journalctl -u s905x-agent -f"

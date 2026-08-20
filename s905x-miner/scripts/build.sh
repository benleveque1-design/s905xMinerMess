#!/usr/bin/env bash
set -e

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$DIR"

echo "=========================================================="
echo " Building S905X Bitcoin SHA-256d Native Miner Binary"
echo "=========================================================="

ARCH=$(uname -m)
echo "[+] Detected architecture: $ARCH"

if [ "$ARCH" = "aarch64" ]; then
    echo "[+] Compiling with ARMv8 Cryptography Extensions hardware acceleration (-march=armv8-a+crypto)..."
    gcc -O3 -march=armv8-a+crypto -pthread -Wall -Wextra -o bitcoin_sha256d_s905x bitcoin_sha256d_s905x.c
else
    echo "[!] Not on AArch64 ($ARCH). Compiling generic portable C binary..."
    gcc -O3 -pthread -Wall -Wextra -o bitcoin_sha256d_s905x bitcoin_sha256d_s905x.c
fi

chmod +x bitcoin_sha256d_s905x
echo "[✓] Build complete: $DIR/bitcoin_sha256d_s905x"

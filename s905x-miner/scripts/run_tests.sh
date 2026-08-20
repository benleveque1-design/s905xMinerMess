#!/usr/bin/env bash
set -e

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$DIR"

if [ ! -f "$DIR/bitcoin_sha256d_s905x" ]; then
    echo "[!] Binary not found. Building now..."
    "$DIR/scripts/build.sh"
fi

echo "=========================================================="
echo " Executing Bitcoin SHA-256d Test Suite"
echo "=========================================================="
"$DIR/bitcoin_sha256d_s905x" -t

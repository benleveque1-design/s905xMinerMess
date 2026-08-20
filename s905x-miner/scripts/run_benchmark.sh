#!/usr/bin/env bash
set -e

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$DIR"

if [ ! -f "$DIR/bitcoin_sha256d_s905x" ]; then
    echo "[!] Binary not found. Building now..."
    "$DIR/scripts/build.sh"
fi

ITERATIONS=${1:-20000000}
THREADS=${2:-3}
CONTROL_CORE=${3:-3}

echo "=========================================================="
echo " Running Multi-Core Benchmark"
echo " Target Iterations : $ITERATIONS"
echo " Hashing Threads   : $THREADS (Cores 0-$((THREADS-1)))"
echo " Control Core      : $CONTROL_CORE"
echo "=========================================================="

"$DIR/bitcoin_sha256d_s905x" -b -n "$ITERATIONS" -j "$THREADS" -c "$CONTROL_CORE"

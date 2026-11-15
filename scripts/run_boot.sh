#!/bin/bash
# Run the native ProStreet runtime and capture the boot report.
#
#   scripts/run_boot.sh [pe-image] [timeout-seconds]
#
# Logs to out/boot.log; prints the tail of the log when it finishes.
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
PE="${1:-artifacts/nfs.exe.unpacked}"
LIMIT="${2:-300}"
LOG="${LOG:-$ROOT/out/boot.log}"

mkdir -p "$(dirname "$LOG")"
echo "== running $ROOT/out/prostreet $PE (timeout ${LIMIT}s) =="
timeout "$LIMIT" ./out/prostreet "$PE" >"$LOG" 2>&1
rc=$?
echo "== exit $rc, log: $LOG ($(wc -l <"$LOG") lines) =="
tail -60 "$LOG"
exit $rc

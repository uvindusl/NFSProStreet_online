#!/bin/bash
# Lift Need for Speed: ProStreet (nfsps.exe, x86-32) to C.
#
#   scripts/lift.sh [path/to/nfsps.exe]
#
# Pipeline: scan non-executable sections for pointers into .text (no base
# relocations in this image) -> drop seeds that point into the middle of an
# instruction -> lift every function to C with the forked nfsu2-web lifter.
#
# Output: work/gen/shard_*.c + decls.h + fntab.c  (never commit: derived from
# the game, exactly like the lifted game code in nfsu2-web).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
EXE="${1:-$ROOT/game/nfsps.exe}"
NSHARD="${NSHARD:-32}"
PY="$ROOT/.venv/bin/python"

[ -f "$EXE" ] || { echo "usage: $0 path/to/nfsps.exe"; exit 1; }
[ -x "$PY" ] || { echo "missing $PY: python3 -m venv .venv && .venv/bin/pip install -r ref-nfsu2-web/requirements.txt"; exit 1; }

mkdir -p "$ROOT/work" "$ROOT/work/gen"

"$PY" "$ROOT/scripts/seed_scan.py"  "$EXE" 16 "$ROOT/work/seeds.txt"
"$PY" "$ROOT/scripts/curate_seeds.py" "$EXE" "$ROOT/work/seeds.txt" "$ROOT/work/seeds_clean.txt"
: > "$ROOT/work/relocs.txt"

echo "lifting $EXE -> $ROOT/work/gen ($NSHARD shards)"
GENOUT="$ROOT/work/gen" \
RELOCS="$ROOT/work/relocs.txt" \
SEEDS="$ROOT/work/seeds.txt" \
JTSWITCH=1 \
  "$PY" "$ROOT/tools/genall.py" "$EXE" "$NSHARD" | tee "$ROOT/work/lift.log"

echo
"$PY" "$ROOT/scripts/audit_lift.py" "$ROOT/work/gen/shard_*.c" | tee "$ROOT/work/audit.txt"
"$PY" "$ROOT/scripts/classify_traps.py" "$ROOT/work/gen" "$EXE" | tee "$ROOT/work/traps.txt"

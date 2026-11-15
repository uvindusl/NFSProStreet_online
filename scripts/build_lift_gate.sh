#!/bin/bash
# Phase 3 compile gate: build the lifted C and prove every entry resolves.
#
#   scripts/build_lift_gate.sh [gen-dir] [analysis.json]
#
# Compiles every shard plus fntab.c against the CPU runtime contract in
# lifter/runtime/, links a small driver that prints g_fn_count, runs it, and
# runs scripts/verify_lift.py.  Exits non-zero on the first failure.
#
# Set FULL=1 to ignore cached objects.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
GEN="${1:-$ROOT/out/lift-reachable}"
ANALYSIS="${2:-$ROOT/out/analysis.json}"
NAME="$(basename "$GEN")"
OBJ="${OBJ:-$ROOT/out/obj-$NAME}"
BIN="${BIN:-$ROOT/out/$NAME-gate}"
J="${J:-$(nproc)}"

mkdir -p "$OBJ"
cd "$ROOT"

compile_one() {
    local src="$1" obj="$2"
    shift 2
    if [ -z "${FULL:-}" ] && [ -f "$obj" ] && [ "$obj" -nt "$src" ] \
       && [ "$obj" -nt "$ROOT/lifter/runtime/cpu.h" ]; then
        return 0
    fi
    local out
    out=$(gcc -O1 -w -c "$@" -o "$obj" "$src" 2>&1) || {
        printf '%s\n' "$out" | head -20
        echo "DID NOT COMPILE: $src" >&2
        return 1
    }
}
export -f compile_one
export ROOT OBJ FULL

echo "== compiling $(ls "$GEN"/shard_*.c | wc -l) shards with $J jobs =="
time ls "$GEN"/shard_*.c | \
    xargs -P "$J" -n1 sh -c 'compile_one "$1" "$OBJ/$(basename "$1" .c).o" -I "$ROOT/lifter/runtime" -I "$GEN"' sh

echo "== fntab, runtime stubs, driver =="
compile_one "$GEN/fntab.c" "$OBJ/fntab.o" -I "$ROOT/lifter/runtime" -I "$GEN"
compile_one "$ROOT/lifter/runtime/stubs.c" "$OBJ/stubs.o" -I "$ROOT/lifter/runtime"
compile_one "$ROOT/lifter/runtime/main.c" "$OBJ/main.o" -I "$ROOT/lifter/runtime" -I "$GEN"

echo "== linking $BIN =="
gcc -no-pie -o "$BIN" "$OBJ"/*.o -lm
"$BIN"

echo "== verifying coverage and traps =="
python3 "$ROOT/scripts/verify_lift.py" "$GEN" "$ANALYSIS"

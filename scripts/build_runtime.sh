#!/bin/bash
# Build the ProStreet native runtime: lifted shards + HLE + boot driver.
#
#   scripts/build_runtime.sh [gen-dir]
#
# Produces out/prostreet.  Set FULL=1 to ignore cached objects, J=<n> to
# choose the parallelism.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
GEN="${1:-$ROOT/out/lift-boot}"
OBJ="${OBJ:-$ROOT/out/obj-runtime}"
BIN="${BIN:-$ROOT/out/prostreet}"
J="${J:-$(nproc)}"
CC="${CC:-gcc}"
CFLAGS="${CFLAGS:--O1 -w -fno-strict-aliasing -fwrapv -fno-stack-protector}"
# The hand-written runtime is compiled with warnings on: -w once hid a GCC
# nested-function error in mem.c that broke the build in a much worse way.
RTFLAGS="${RTFLAGS:--O1 -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable -Wno-unused-but-set-variable -fno-strict-aliasing -fwrapv -fno-stack-protector}"

mkdir -p "$OBJ"
cd "$ROOT"

compile_one() {
    local src="$1" obj="$2"
    shift 2
    if [ -z "${FULL:-}" ]; then
        if [ -f "$obj" ] && [ "$obj" -nt "$src" ]; then
            local dep fresh=1
            for dep in ${DEPS:-}; do
                [ "$obj" -nt "$dep" ] || { fresh=0; break; }
            done
            [ "$fresh" = 1 ] && return 0
        fi
    fi
    local out
    out=$($CC "$@" -c "$src" -o "$obj" 2>&1) || {
        printf '%s\n' "$out" | head -30
        echo "DID NOT COMPILE: $src" >&2
        return 1
    }
    # warnings are errors for the hand-written runtime
    if [ -n "${STRICT:-}" ] && printf '%s\n' "$out" | grep -q "warning:"; then
        printf '%s\n' "$out" | head -30
        echo "WARNINGS in $src" >&2
        return 1
    fi
}
export -f compile_one
export ROOT OBJ FULL CC CFLAGS RTFLAGS STRICT GEN

echo "== compiling $(ls "$GEN"/shard_*.c | wc -l) shards with $J jobs =="
export DEPS="$ROOT/lifter/runtime/cpu.h $GEN/decls.h"
time ls "$GEN"/shard_*.c | \
    xargs -P "$J" -n1 bash -c 'compile_one "$1" "$OBJ/$(basename "$1" .c).o" $CFLAGS -I "$ROOT/lifter/runtime" -I "$GEN"' sh

# MEMCHK_SHARDS="0049 0050" (debugging): rebuild those shards with every
# guest access checked (cpu.h PROSTREET_MEMCHK, PROSTREET_MEMCHK_WATCH);
# a later build without it rebuilds them normally.
for sh in ${MEMCHK_SHARDS:-}; do
    rm -f "$OBJ/shard_$sh.o"
    compile_one "$GEN/shard_$sh.c" "$OBJ/shard_$sh.o" $CFLAGS -DPROSTREET_MEMCHK -I "$ROOT/lifter/runtime" -I "$GEN" && touch "$OBJ/.memchk_$sh"
done
for m in "$OBJ"/.memchk_*; do
    [ -e "$m" ] || continue
    sh="${m##*/.memchk_}"
    case " ${MEMCHK_SHARDS:-} " in *" $sh "*) ;; *) rm -f "$m" "$OBJ/shard_$sh.o"; compile_one "$GEN/shard_$sh.c" "$OBJ/shard_$sh.o" $CFLAGS -I "$ROOT/lifter/runtime" -I "$GEN" ;; esac
done

echo "== function table =="
compile_one "$GEN/fntab.c" "$OBJ/fntab.o" $CFLAGS -I "$ROOT/lifter/runtime" -I "$GEN"

# AEMS audio module banks lifted by scripts/lift_aems.sh (game-derived, in out/)
AEMS="${AEMS:-$ROOT/out/lift-aems}"
if [ -f "$AEMS/aems_table.c" ]; then
    echo "== AEMS banks ($(ls "$AEMS"/bank_*.c 2>/dev/null | wc -l)) =="
    export DEPS="$ROOT/lifter/runtime/cpu.h"
    rm -f "$OBJ"/aems_*.o
    for src in "$AEMS"/*.c; do
        compile_one "$src" "$OBJ/aems_$(basename "$src" .c).o" $CFLAGS ${AEMS_CFLAGS:-} -I "$ROOT/lifter/runtime"
    done
else
    rm -f "$OBJ"/aems_*.o
fi

echo "== runtime =="
export DEPS="$ROOT/lifter/runtime/rt.h $ROOT/lifter/runtime/cpu.h"
for src in mem hdfont gamefixes aems autotest dispatch win32 impargs boot d3d9_hle d3dx9_hle d3dx_effect swrast com; do
    compile_one "$ROOT/lifter/runtime/$src.c" "$OBJ/rt_$src.o" $RTFLAGS -I "$ROOT/lifter/runtime" -I "$GEN"
done

echo "== SEH entry points =="
export DEPS="$ROOT/lifter/runtime/cpu.h $GEN/decls.h"
compile_one "$ROOT/lifter/runtime/seh_lifted.c" "$OBJ/rt_seh_lifted.o" $CFLAGS -I "$ROOT/lifter/runtime" -I "$GEN"

echo "== linking $BIN =="
$CC -no-pie -o "$BIN" "$OBJ"/*.o -lm -lpthread
ls -l "$BIN"

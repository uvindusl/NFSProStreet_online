#!/bin/bash
# Compile every generated shard for the host.  This is a syntax/ABI check on
# the lifter's output (no link, no runtime): if this passes, the lift is
# well-formed C against runtime/cpu.h.
#
#   scripts/host_compile_check.sh [gen dir] [opt]
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
GEN="${1:-$ROOT/work/gen}"
OPT="${2:--O1}"
REF="${REF:-$ROOT/ref-nfsu2-web}"
OUT="$ROOT/work/hostobj"
rm -rf "$OUT"; mkdir -p "$OUT"
ls "$GEN"/shard_*.c >/dev/null

start=$(date +%s)
ls "$GEN"/shard_*.c | xargs -P "$(nproc)" -I{} sh -c \
  'clang '"$OPT"' -Wno-unused-variable -Wno-unused-label -c -I "$1/runtime" -I "$2" "$3" -o "$4/$(basename "$3" .c).o"' \
  _ "$REF" "$GEN" {} "$OUT"
end=$(date +%s)
echo "compiled $(ls "$OUT" | wc -l | tr -d ' ') shards in $((end - start))s -> $OUT"
du -sh "$OUT"

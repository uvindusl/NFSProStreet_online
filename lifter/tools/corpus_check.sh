#!/bin/sh
# corpus_check.sh - translate all shader blobs in DIR to WGSL and run shader-check.
#
# Usage: corpus_check.sh [SHADER_DIR] [DUMP_DIR]
#   SHADER_DIR defaults to /tmp/opencode/ps-shaders
#   DUMP_DIR   defaults to /tmp/opencode/ps-dump-repro
#
# Builds out/tools/shader2wgsl (via lifter/tools/build_shader2wgsl.sh) if missing,
# builds mkdump, writes built-in modules vs_0.wgsl, ps_0.wgsl, pulltest.wgsl into
# DUMP_DIR, then translates every vs_*.bin and ps_*.bin blob into vs_N.wgsl /
# ps_N.wgsl with sequential N from 1, renaming the emitted entry point vs_0/ps_0
# to vs_N/ps_N.
#
# NOTE: no built-in ps_256 is written (a corpus blob numbered 256 would collide);
# the cube built-in is deliberately omitted here (add ps_2000.wgsl if needed).

set -eu

SHADER_DIR="${1:-/tmp/opencode/ps-shaders}"
DUMP_DIR="${2:-/tmp/opencode/ps-dump-repro}"
REPO="$(cd "$(dirname "$0")/../.." && pwd)"

mkdir -p "$DUMP_DIR"

# Build out/tools/shader2wgsl if missing
if [ ! -x "$REPO/out/tools/shader2wgsl" ]; then
    "$REPO/lifter/tools/build_shader2wgsl.sh"
fi

# Build mkdump
gcc -O2 -DVSEMU_WGSL -I "$REPO/web/runtime" -I "$REPO/web/runtime/web" -I "$REPO/web/runtime/web/runtime" \
    -o "$DUMP_DIR/mkdump" "$REPO/lifter/tools/mkdump.c" "$REPO/web/runtime/web/runtime/vsemu.c" -lm

# Write built-in modules
"$DUMP_DIR/mkdump" "$DUMP_DIR"

# Translate all vertex shaders: vs_1.wgsl .. vs_N.wgsl, renaming entry vs_0 -> vs_N
i=1
for f in "$SHADER_DIR"/vs_*.bin; do
    [ -e "$f" ] || continue
    out="$("$REPO/out/tools/shader2wgsl" "$f" -o /dev/stdout 2>/dev/null)" || { echo "FAIL: shader2wgsl $f" >&2; exit 1; }
    echo "$out" | sed "s/vs_0/vs_$i/g" > "$DUMP_DIR/vs_$i.wgsl"
    i=$((i + 1))
done

# Translate all pixel shaders: ps_1.wgsl .. ps_N.wgsl, renaming entry ps_0 -> ps_N
j=1
for f in "$SHADER_DIR"/ps_*.bin; do
    [ -e "$f" ] || continue
    out="$("$REPO/out/tools/shader2wgsl" "$f" -o /dev/stdout 2>/dev/null)" || { echo "FAIL: shader2wgsl $f" >&2; exit 1; }
    echo "$out" | sed "s/ps_0/ps_$j/g" > "$DUMP_DIR/ps_$j.wgsl"; j=$((j + 1))
done

echo "Wrote $((i - 1)) vertex shaders and $((j - 1)) pixel shaders to $DUMP_DIR"

# Run the WebGPU shader checker
node "$REPO/web/tools/shader-check.mjs" --from "$DUMP_DIR" --query '&all2d' --timeout 1200000
exit $?

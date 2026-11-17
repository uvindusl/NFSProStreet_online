#!/bin/sh
# shader_vec_diff.sh - build the WGSL vector dump for a corpus of shader blobs
# and run the numeric differential (WGSL emitter vs the CPU oracle).
#
# Usage: shader_vec_diff.sh [SHADER_DIR] [VEC_DIR]
#   SHADER_DIR defaults to /tmp/opencode/ps-shaders
#   VEC_DIR    defaults to /tmp/opencode/ps-vec-dump
#
# What it does:
#   1. builds web/tools/wgsl-xl (the reference-vector generator) with its
#      --corpus mode;
#   2. runs `wgsl-xl --corpus SHADER_DIR VEC_DIR`, which writes the built-in
#      modules plus, for every vs_*.bin / ps_*.bin blob, the emitted module
#      (vs_N.wgsl / ps_N.wgsl) and its differential vectors (vs_N.vec /
#      ps_N.pvec).  The oracle is the CPU interpreter: vs_run for vertices and
#      ps_ref_run for pixels, on the same bytecode and the same synthetic
#      vertex declaration / constants / textures;
#   3. runs web/tools/shader-check.mjs over VEC_DIR with &all2d (the corpus has
#      no cube twins: pixel ids run 1..N).
#
# NOTE: SHADER_DIR is game-derived and is NOT in the repository; point the
# script at your own dump of vs_*.bin / ps_*.bin blobs.
#
# Exit status is the shader-check status.  As of this commit the differential
# is EXPECTED to be non-zero: the corpus exercises more texture coordinates
# (oT4..oT7 / a4..a7) and samplers (s4..s6) than the current 4-wide ABI
# carries, plus a handful of oracle/emitter divergences.  See
# debug/shader-differential.md for the taxonomy.

set -eu

SHADER_DIR="${1:-/tmp/opencode/ps-shaders}"
VEC_DIR="${2:-/tmp/opencode/ps-vec-dump}"
REPO="$(cd "$(dirname "$0")/../.." && pwd)"

mkdir -p "$VEC_DIR" "$REPO/out/tools"

echo "== building wgsl-xl (--corpus) =="
gcc -O2 -DVSEMU_WGSL -DNFS_ENHANCED=0 \
    -I "$REPO/web/runtime" -I "$REPO/web/runtime/web" -I "$REPO/web/runtime/web/runtime" \
    -o "$REPO/out/tools/wgsl-xl" \
    "$REPO/web/tools/wgsl-xl.c" "$REPO/web/runtime/web/runtime/vsemu.c" -lm

echo "== generating modules + vectors: $SHADER_DIR -> $VEC_DIR =="
"$REPO/out/tools/wgsl-xl" --corpus "$SHADER_DIR" "$VEC_DIR"

echo "== differential: WGSL emitter vs CPU oracle =="
node "$REPO/web/tools/shader-check.mjs" --from "$VEC_DIR" --query '&all2d' --timeout 1800000
exit $?

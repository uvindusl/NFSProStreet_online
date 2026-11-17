#!/bin/bash
# Validate every extracted ProStreet shader: translate D3D9 bytecode -> WGSL
# with the offline translator, then run naga on each module.
#
#   scripts/validate_wgsl.sh [shader-dir]
#
# shader-dir defaults to artifacts/shaders and must contain the <profile>_<n>.bin
# blobs written by lifter/tools/extract_shaders.py.  Nothing here is committed:
# the blobs come from your own game image and the WGSL is written to out/wgsl/.
#
# naga is looked up in PATH, then in ~/.cargo/bin.  Without it only the
# translation half runs.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

SRC="${1:-artifacts/shaders}"
OUT="out/wgsl"
BIN="out/tools/shader2wgsl"

if [ ! -x "$BIN" ]; then
    echo "building $BIN ..."
    bash lifter/tools/build_shader2wgsl.sh || exit 1
fi

if [ ! -d "$SRC" ]; then
    echo "no shader directory: $SRC" >&2
    echo "extract it first: python3 lifter/tools/extract_shaders.py <pe-image> $SRC" >&2
    exit 1
fi

NAGA="$(command -v naga || true)"
if [ -z "$NAGA" ] && [ -x "$HOME/.cargo/bin/naga" ]; then
    NAGA="$HOME/.cargo/bin/naga"
fi
if [ -z "$NAGA" ]; then
    echo "warning: naga not found; running translation only" >&2
fi

mkdir -p "$OUT"
total=0; emitfail=0; nagafail=0
for f in "$SRC"/*.bin; do
    [ -e "$f" ] || continue
    b="$(basename "$f" .bin)"
    if "$BIN" "$f" -o "$OUT/$b.wgsl" >/dev/null 2>&1; then
        total=$((total + 1))
        if [ -n "$NAGA" ] && ! "$NAGA" "$OUT/$b.wgsl" >/dev/null 2>&1; then
            echo "NAGA-FAIL $b"; nagafail=$((nagafail + 1))
        fi
    else
        echo "EMIT-FAIL $b"; emitfail=$((emitfail + 1))
    fi
done
echo "=== emitted=$total emitfail=$emitfail naga_fail=$nagafail ==="
[ "$emitfail" -eq 0 ] && [ "$nagafail" -eq 0 ]

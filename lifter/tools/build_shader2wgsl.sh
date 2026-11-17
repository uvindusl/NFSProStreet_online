#!/bin/bash
# Build lifter/tools/shader2wgsl: an offline D3D9 shader bytecode -> WGSL
# translator.  Links the reference parser/emitter (vsemu.c compiled with
# -DVSEMU_WGSL, which pulls in web/runtime/web/vsemu_wgsl.inc) and adds the
# offline driver in lifter/tools/shader2wgsl.c.
#
#   ./lifter/tools/build_shader2wgsl.sh            # build
#   ./lifter/tools/build_shader2wgsl.sh --selftest # build and run the selftest
set -euo pipefail
cd "$(dirname "$0")/../.."

OUT=out/tools
mkdir -p "$OUT"

gcc -O2 -DVSEMU_WGSL \
    -I web/runtime -I web/runtime/web -I web/runtime/web/runtime \
    -o "$OUT/shader2wgsl" \
    lifter/tools/shader2wgsl.c \
    web/runtime/web/runtime/vsemu.c \
    -lm

echo "built $OUT/shader2wgsl"

if [ "${1:-}" = "--selftest" ]; then
    "$OUT/shader2wgsl" --selftest --test >/dev/null
    echo "selftest: PASS"
fi

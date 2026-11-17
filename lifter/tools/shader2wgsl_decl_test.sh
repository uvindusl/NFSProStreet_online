#!/bin/sh
# shader2wgsl_decl_test.sh - prove --decl feeds a real vertex declaration
# into vs_to_wgsl instead of the zero placeholder.
#
# vs_1_1_001 is a vertex shader; without --decl every v# reads FLOAT1 at
# offset 0.  With --decl 0:0:3,1:16:4,2:20:1 the reads become:
#   v0 = vec4f(ldf(B + 0u), ldf(B + 4u), ldf(B + 8u), ldf(B + 12u))   (FLOAT4)
#   v1 = ldcolor(B + 16u)                                            (D3DCOLOR)
#   v2 = vec4f(ldf(B + 20u), ldf(B + 24u), 0.0, 1.0)                 (FLOAT3)
# Exit non-zero if any of the three is missing from the emitted WGSL.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
BIN="$ROOT/out/tools/shader2wgsl"
BLOB="${1:-/tmp/opencode/ps-shaders/vs_1_1_001.bin}"

if [ ! -x "$BIN" ]; then
    echo "shader2wgsl_decl_test: no executable at $BIN" >&2
    exit 1
fi

OUT=$("$BIN" "$BLOB" --decl 0:0:3,1:16:4,2:20:1) || {
    echo "shader2wgsl_decl_test: translator failed" >&2
    exit 1
}

fail=0
check() {
    case "$OUT" in
        *"$1"*) ;;
        *) echo "shader2wgsl_decl_test: MISSING: $1" >&2; fail=1 ;;
    esac
}

check "vec4f(ldf(B + 0u), ldf(B + 4u), ldf(B + 8u), ldf(B + 12u))"
check "ldcolor(B + 16u)"
check "vec4f(ldf(B + 20u), ldf(B + 24u), 0.0, 1.0)"

if [ "$fail" -eq 0 ]; then
    echo "shader2wgsl_decl_test: OK (--decl reads v0 FLOAT4, v1 D3DCOLOR, v2 FLOAT3)"
else
    echo "shader2wgsl_decl_test: FAIL" >&2
fi
exit "$fail"
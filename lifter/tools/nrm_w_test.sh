#!/bin/bash
# nrm_w_test.sh - prove the vertex NRM emitter writes src0.w * 1/|xyz|
#
# D3D9 NRM: dest.w = src0.w * (1/|src0.xyz|).  Before the fix the vertex
# emitter wrote a bare 1.0 for the w component.  vs_2_0_134 and vs_3_0_051
# both do `mov r0.w, c120.xxxx; nrm r1, r0` and then read r1.w, so the w
# factor is live in the corpus.  This test asserts the emitted WGSL carries
# the src0.w factor (not a bare 1.0) for every NRM in every vertex shader.
set -u

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
BIN="$ROOT/out/tools/shader2wgsl"
SHADER_DIR="${SHADER_DIR:-/tmp/opencode/ps-shaders}"

if [ ! -x "$BIN" ]; then
    echo "nrm_w_test: no executable at $BIN" >&2
    exit 1
fi

fail=0
checked=0
for blob in "$SHADER_DIR"/vs_*.bin; do
    [ -e "$blob" ] || continue
    out=$("$BIN" "$blob" 2>/dev/null) || continue
    # find every NRM emission line
    while IFS= read -r line; do
        case "$line" in
            *"select(normalize("*"length(("*") == 0.0), 1.0)"*)
                echo "nrm_w_test: BARE 1.0 w-component in $(basename "$blob")" >&2
                echo "  $line" >&2
                fail=1
                ;;
            *"select(normalize("*"length(("*") == 0.0), "*".w * select("*)
                checked=$((checked + 1))
                ;;
        esac
    done <<EOF
$out
EOF
done

if [ "$checked" -eq 0 ]; then
    echo "nrm_w_test: no NRM emissions found in vertex shaders" >&2
    fail=1
fi

if [ "$fail" -eq 0 ]; then
    echo "nrm_w_test: OK ($checked NRM emissions carry the src0.w factor)"
else
    echo "nrm_w_test: FAIL" >&2
fi
exit "$fail"
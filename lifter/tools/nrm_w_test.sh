#!/bin/bash
# nrm_w_test.sh - prove the vertex NRM emitter writes src0.w * 1/|xyz|
#
# D3D9 NRM: dest.w = src0.w * (1/|src0.xyz|).  Before the fix the vertex
# emitter wrote a bare 1.0 for the w component.  This is a TEXT check over the
# emitted WGSL: for every NRM in every vertex shader it asserts the emission
# carries the src0.w factor rather than a bare 1.0.
#
# The corpus cannot OBSERVE the w factor numerically: the only two vertex
# shaders with an unmasked `nrm` (vs_2_0_134, vs_3_0_051) contain
# `def c120 = 0 4 1 0`, which overrides the caller's c120, so their nrm source
# w is 0 and the result w is 0 either way.  For the numeric check use
# lifter/tools/nrm_w_all_test.sh, whose CHECK 3 runs a synthetic shader
# (oT0.w == src0.w * 1/|xyz| == 0.4).
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
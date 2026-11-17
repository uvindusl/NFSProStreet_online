#!/bin/bash
# nrm_w_all_test.sh - prove the vertex NRM w factor is correct in ALL three implementations
# (1) WGSL emitter via shader2wgsl   - text check over the corpus
# (2) MSL emitter via vs_to_msl      - text check over the corpus
# (3) CPU vs_run numerically         - synthetic shader (the corpus cannot observe it)

set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
SHADER2WGSL="$ROOT/out/tools/shader2wgsl"
SHADER_DIR="${SHADER_DIR:-/tmp/opencode/ps-shaders}"

# Build directories
BUILD_DIR="$HERE/build"
mkdir -p "$BUILD_DIR"

# ============================================================
# CHECK 1: WGSL emitter (same as nrm_w_test.sh)
# ============================================================
check_wgsl() {
    echo "=== CHECK 1: WGSL emitter (shader2wgsl) ==="
    if [ ! -x "$SHADER2WGSL" ]; then
        echo "  FAIL: no executable at $SHADER2WGSL" >&2
        return 1
    fi

    fail=0
    checked=0
    for blob in "$SHADER_DIR"/vs_*.bin; do
        [ -e "$blob" ] || continue
        out=$("$SHADER2WGSL" "$blob" 2>/dev/null) || continue
        while IFS= read -r line; do
            case "$line" in
                *"select(normalize("*"length(("*") == 0.0), 1.0)"*)
                    echo "  WGSL FAIL: BARE 1.0 w-component in $(basename "$blob")" >&2
                    echo "    $line" >&2
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
        echo "  WGSL FAIL: no NRM emissions found in vertex shaders" >&2
        return 1
    fi

    if [ "$fail" -eq 0 ]; then
        echo "  WGSL OK ($checked NRM emissions carry the src0.w factor)"
        return 0
    fi
    return 1
}

# ============================================================
# CHECK 2: MSL emitter via vs_to_msl
# ============================================================
check_msl() {
    echo "=== CHECK 2: MSL emitter (vs_to_msl) ==="
    
    local msl_driver="$BUILD_DIR/msl_nrm_test"
    local vsemu_obj="$BUILD_DIR/vsemu_msl.o"
    if [ ! -x "$msl_driver" ] || [ ! -f "$vsemu_obj" ]; then
        echo "  Building MSL test driver..."
        local ROOT_DIR=$(cd "$HERE/../.." && pwd)
        gcc -std=c11 -O2 -DVSEMU_MSL \
            -I "$ROOT_DIR/web/runtime" \
            -I "$ROOT_DIR/web/runtime/web" \
            -I "$ROOT_DIR/web/runtime/web/runtime" \
            -c "$ROOT_DIR/web/runtime/web/runtime/vsemu.c" \
            -o "$vsemu_obj" || { echo "  FAIL: vsemu build failed" >&2; return 1; }
        gcc -std=c11 -O2 -DVSEMU_MSL \
            -I "$ROOT_DIR/web/runtime" \
            -I "$ROOT_DIR/web/runtime/web" \
            -I "$ROOT_DIR/web/runtime/web/runtime" \
            -o "$msl_driver" \
            "$HERE/msl_nrm_test.c" \
            "$vsemu_obj" \
            -lm || { echo "  FAIL: build failed" >&2; return 1; }
    fi

    fail=0
    checked=0
    for blob in "$SHADER_DIR"/vs_*.bin; do
        [ -e "$blob" ] || continue
        out=$("$msl_driver" "$blob" 2>/dev/null) || continue
        while IFS= read -r line; do
            case "$line" in
                *"length("*".xyz)==0.0?float3(0.0):normalize("*"xyz), 1.0)"*)
                    echo "  MSL FAIL: BARE 1.0 w-component in $(basename "$blob")" >&2
                    echo "    $line" >&2
                    fail=1
                    ;;
                *"length("*".xyz)==0.0?float3(0.0):normalize("*"xyz), ("*".w * ("*)
                    checked=$((checked + 1))
                    ;;
            esac
        done <<EOF
$out
EOF
    done

    if [ "$checked" -eq 0 ]; then
        echo "  MSL FAIL: no NRM emissions found in vertex shaders" >&2
        return 1
    fi

    if [ "$fail" -eq 0 ]; then
        echo "  MSL OK ($checked NRM emissions carry the src0.w factor)"
        return 0
    fi
    return 1
}

# ============================================================
# CHECK 3: CPU vs_run numerical (synthetic shader)
# ============================================================
# The corpus cannot observe the vertex NRM w factor: the only two vertex
# shaders with an unmasked `nrm` (vs_2_0_134, vs_3_0_051) contain
# `def c120 = 0 4 1 0`, which overrides the caller's c120, so their nrm
# source w is 0 and the result w is 0 either way.  cpu_nrm_test.c therefore
# builds a synthetic vs_2_0 shader (dcl_position v0 ; mov r0, c0 ; nrm r1, r0 ;
# mov oT0, r1) and checks oT0.w == src0.w * 1/|(3,4,0)| == 2 * 0.2 == 0.4.
# Forcing vs_run's NRM d[3] to 1.f makes it print oT0.w = 1 and exit 1.
check_cpu() {
    echo "=== CHECK 3: CPU vs_run numerical (synthetic shader) ==="

    local cpu_driver="$BUILD_DIR/cpu_nrm_test"
    local vsemu="$ROOT/web/runtime/web/runtime/vsemu.c"
    if [ ! -x "$cpu_driver" ] || [ "$HERE/cpu_nrm_test.c" -nt "$cpu_driver" ] \
        || [ "$vsemu" -nt "$cpu_driver" ]; then
        echo "  Building CPU test driver..."
        gcc -O2 \
            -I "$ROOT/web/runtime" \
            -I "$ROOT/web/runtime/web" \
            -I "$ROOT/web/runtime/web/runtime" \
            -o "$cpu_driver" \
            "$HERE/cpu_nrm_test.c" \
            "$vsemu" \
            -lm || { echo "  FAIL: build failed" >&2; return 1; }
    fi

    local out
    if out=$("$cpu_driver" 2>&1); then
        printf '%s\n' "$out" | sed 's/^/  /'
        return 0
    fi
    printf '%s\n' "$out" | sed 's/^/  /' >&2
    return 1
}

# ============================================================
# MAIN
# ============================================================
echo "============================================================"
echo "NRM w-factor all-implementation test"
echo "============================================================"

OVERALL=0

check_wgsl || OVERALL=1
echo ""
check_msl || OVERALL=1
echo ""
check_cpu || OVERALL=1
echo ""

if [ "$OVERALL" -eq 0 ]; then
    echo "============================================================"
    echo "ALL CHECKS PASSED"
    echo "============================================================"
    exit 0
else
    echo "============================================================"
    echo "SOME CHECKS FAILED"
    echo "============================================================"
    exit 1
fi
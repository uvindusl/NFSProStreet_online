#!/bin/bash
# ProStreet Milestone D4: Instruction-level tests for texld, texkill, and R_ADDR (mova)
# Runs specific shaders through the reference interpreter and asserts expected outputs.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOLS_DIR="$SCRIPT_DIR"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SHADER2WGSL="$ROOT/out/tools/shader2wgsl"
SHADER_DIR="${SHADER_DIR:-/tmp/opencode/ps-shaders}"

echo "=== Milestone D4: Instruction-Level Tests ==="
echo ""

PASS=0
FAIL=0

test_instruction() {
    local name="$1"
    local shader="$2"
    local expected_out="$3"
    local desc="$4"
    
    echo "Test: $name"
    echo "  Shader: $shader"
    echo "  Description: $desc"
    
    output=$("$SHADER2WGSL" "$shader" --test 2>&1 | grep "interpreter OK" || true)
    
    if [ -z "$output" ]; then
        echo "  FAIL: --test mode failed or not supported"
        FAIL=$((FAIL + 1))
        return 1
    fi
    
    actual_out=$(echo "$output" | sed -n 's/.*out=\([0-9.]* [0-9.]* [0-9.]* [0-9.]*\).*/\1/p')
    
    echo "  Output: out=$actual_out"
    echo "  Expected: out=$expected_out"
    
    if [ "$actual_out" = "$expected_out" ]; then
        echo "  PASS"
        PASS=$((PASS + 1))
    else
        echo "  FAIL: output mismatch"
        FAIL=$((FAIL + 1))
    fi
    echo ""
}

test_wgsl_translation() {
    local name="$1"
    local shader="$2"
    local pattern="$3"
    local desc="$4"
    
    echo "Test: $name (WGSL translation)"
    echo "  Shader: $shader"
    echo "  Description: $desc"
    
    output=$("$SHADER2WGSL" "$shader" 2>&1)
    
    if echo "$output" | grep -q "$pattern"; then
        echo "  PASS: found '$pattern'"
        PASS=$((PASS + 1))
    else
        echo "  FAIL: pattern '$pattern' not found in WGSL output"
        FAIL=$((FAIL + 1))
    fi
    echo ""
}

# Test 1: texld (O_TEX=66) - ps_2_0_002.bin has "??? r0, a0, s0" which is texld
test_instruction \
    "texld" \
    "$SHADER_DIR/ps_2_0_002.bin" \
    "1.000 1.000 1.000 1.000" \
    "ps_2_0 shader with texture read (texld at instruction 4: '??? r0, a0, s0')"

# Test 2: texld (O_TEX=66) - ps_2_0_005.bin has "??? r0, a0, s0"
test_instruction \
    "texld_2" \
    "$SHADER_DIR/ps_2_0_005.bin" \
    "1.000 1.000 1.000 1.000" \
    "ps_2_0 shader with texture read (texld at instruction 3)"

# Test 3: texld with different output - ps_2_0_008.bin
test_instruction \
    "texld_3" \
    "$SHADER_DIR/ps_2_0_008.bin" \
    "0.000 0.000 0.000 1.000" \
    "ps_2_0 shader with texture read producing black color"

# Test 4: R_ADDR (mova) - vs_2_0_009.bin has "mova a0.x, r0.xxxx"
test_wgsl_translation \
    "R_ADDR_mova" \
    "$SHADER_DIR/vs_2_0_009.bin" \
    "a0 = vec4i" \
    "vs_2_0 shader with mova a0.x, r0.xxxx (address register load)"

# Test 5: R_ADDR relative addressing - check a0.x usage in indexing
test_wgsl_translation \
    "R_ADDR_relative" \
    "$SHADER_DIR/vs_2_0_009.bin" \
    "clamp(a0.x" \
    "vs_2_0 shader using a0.x for relative constant indexing"

# Test 6: R_ADDR in another shader - vs_2_0_012.bin
test_wgsl_translation \
    "R_ADDR_mova_2" \
    "$SHADER_DIR/vs_2_0_012.bin" \
    "a0 = vec4i" \
    "vs_2_0 shader with mova a0.x, r0.xxxx"

# Test 7: R_ADDR with different swizzle - vs_2_0_017.bin has "mova a0.x, r0.wwww"
test_wgsl_translation \
    "R_ADDR_mova_wwww" \
    "$SHADER_DIR/vs_2_0_017.bin" \
    "a0 = vec4i" \
    "vs_2_0 shader with mova a0.x, r0.wwww"

echo "=== Summary ==="
echo "Passed: $PASS"
echo "Failed: $FAIL"

if [ $FAIL -gt 0 ]; then
    exit 1
fi
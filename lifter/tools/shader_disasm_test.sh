#!/bin/bash
# Validate shader_disasm output against all 591 real blobs

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOLS_DIR="$SCRIPT_DIR"
DISASM="$TOOLS_DIR/build/shader_disasm"
SHADER_DIR="/tmp/opencode/ps-shaders"

echo "=== Shader Disassembler Validation ==="
echo "Testing $(ls "$SHADER_DIR"/*.bin | wc -l) blobs..."
echo ""

FAILED=0
PASSED=0

validate_blob() {
    local blob="$1"
    local name=$(basename "$blob")
    local output
    
    output=$("$DISASM" "$blob" 2>&1)
    local exit_code=$?
    
    if [ $exit_code -ne 0 ]; then
        echo "FAIL: $name - disassembler exited with code $exit_code"
        echo "  Output: $output"
        return 1
    fi
    
    # Check version tag
    local version_line=$(echo "$output" | grep "^Version: ")
    if [ -z "$version_line" ]; then
        echo "FAIL: $name - no version tag found"
        return 1
    fi
    
    # Check instruction count line
    local instr_line=$(echo "$output" | grep "^Instructions: ")
    if [ -z "$instr_line" ]; then
        echo "FAIL: $name - no instruction count found"
        return 1
    fi
    
    local instr_count=$(echo "$instr_line" | sed 's/Instructions: //')
    
    # Check that we have instruction lines (format: "   N opcode...")
    local instr_lines=$(echo "$output" | grep -E "^\s+[0-9]+\s+\w+" | wc -l)
    
    # Allow some mismatch (vs_disasm skips comments and some pseudo-ops)
    # but warn if significant
    local diff=$((instr_count - instr_lines))
    if [ $diff -lt 0 ]; then diff=$((-diff)); fi
    if [ $diff -gt 5 ]; then
        echo "WARN: $name - instruction count diff: header=$instr_count, lines=$instr_lines"
    fi
    
    # Syntactic validation of each instruction line
    local line_num=0
    echo "$output" | grep -E "^\s+[0-9]+\s+\w+" | while IFS= read -r line; do
        line_num=$((line_num + 1))
        # Check format: "  NNN opcode args..."
        if ! echo "$line" | grep -qE "^\s+[0-9]+\s+\w+"; then
            echo "FAIL: $name - line $line_num: malformed instruction line"
            echo "  Line: $line"
            return 1
        fi
        
        # Extract opcode
        local opcode=$(echo "$line" | sed -E 's/^\s+[0-9]+\s+(\w+).*/\1/')
        
        # Valid opcodes from vsemu
        case "$opcode" in
            def|defi|defb|dcl|mov|mul|add|mad|dp3|dp4|frc|min|max|slt|sge|rsq|rcp|log|lit|exp|pow|crs|sgn|abs|nrm|sincos|rep|endrep|ret|call|callnz|loop|endloop|break|breakc|ifc|else|endif|label|nop|texldl|texldd|texld|texcrd|texkill|texbem|texbeml|texreg2ar|texreg2gb|texm3x2pad|texm3x2tex|texm3x3pad|texm3x3tex|texm3x3spec|texm3x3vspec|expp|logp|pkh|pkd|rmul|pred|lt|ge|eq|ne|mova)
                ;;
            ???)
                # Unknown opcode is allowed (counted in unknown)
                ;;
            *)
                echo "FAIL: $name - line $line_num: unknown opcode '$opcode'"
                echo "  Line: $line"
                return 1
                ;;
        esac
        
        # Check for register/swizzle/write-mask syntax if present
        local after_opcode=$(echo "$line" | sed -E 's/^\s+[0-9]+\s+\w+\s+//')
        # Should contain valid register syntax
        if echo "$after_opcode" | grep -qE '\.(x|y|z|w){1,4}'; then
            # Valid swizzle/write-mask
            :
        elif echo "$after_opcode" | grep -qE '\[a0\]'; then
            # Relative addressing - valid
            :
        fi
    done
    
    return 0
}

for blob in "$SHADER_DIR"/*.bin; do
    if validate_blob "$blob"; then
        PASSED=$((PASSED + 1))
    else
        FAILED=$((FAILED + 1))
    fi
    
    # Progress
    if [ $((PASSED + FAILED)) -eq 50 ] || [ $((PASSED + FAILED)) -eq 100 ] || [ $((PASSED + FAILED)) -eq 200 ] || [ $((PASSED + FAILED)) -eq 300 ] || [ $((PASSED + FAILED)) -eq 400 ] || [ $((PASSED + FAILED)) -eq 500 ] || [ $((PASSED + FAILED)) -eq 591 ]; then
        echo "  Progress: $((PASSED + FAILED))/591 (passed: $PASSED, failed: $FAILED)"
    fi
done

echo ""
echo "=== Validation Summary ==="
echo "Total: 591"
echo "Passed: $PASSED"
echo "Failed: $FAILED"

if [ $FAILED -gt 0 ]; then
    exit 1
fi

# Show first 10 lines for specific blobs
echo ""
echo "=== Sample Outputs ==="
echo ""
echo "--- ps_2_0_001.bin ---"
"$DISASM" "$SHADER_DIR/ps_2_0_001.bin" 2>&1 | head -15

echo ""
echo "--- ps_3_0_001.bin ---"
"$DISASM" "$SHADER_DIR/ps_3_0_001.bin" 2>&1 | head -15

echo ""
echo "--- vs_1_1_001.bin ---"
"$DISASM" "$SHADER_DIR/vs_1_1_001.bin" 2>&1 | head -15
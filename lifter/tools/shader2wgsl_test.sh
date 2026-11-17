#!/bin/bash
# Validate shader2wgsl translation against all 591 real blobs

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOLS_DIR="$SCRIPT_DIR"
ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
SHADER2WGSL="$ROOT/out/tools/shader2wgsl"
SHADER_DIR="${SHADER_DIR:-/tmp/opencode/ps-shaders}"

echo "=== Shader2WGSL Validation ==="
echo "Testing $(ls "$SHADER_DIR"/*.bin | wc -l) blobs..."
echo ""

TOTAL=0
PASSED=0
FAILED=0
FAILED_ODEPTH=0
FAILED_OTHER=0

declare -A FAILED_BY_PROFILE

for blob in "$SHADER_DIR"/*.bin; do
    name=$(basename "$blob")
    TOTAL=$((TOTAL + 1))
    
    # Extract profile from filename
    if [[ "$name" =~ ^(vs|ps)_[0-9]+_[0-9]+_ ]]; then
        profile="${BASH_REMATCH[0]%_}"
    else
        profile="unknown"
    fi
    
    output=$("$SHADER2WGSL" "$blob" 2>&1)
    exit_code=$?
    
    if [ $exit_code -eq 0 ]; then
        # Check if output contains oDepth-related error
        if echo "$output" | grep -qi "odepth\|oDepth"; then
            FAILED=$((FAILED + 1))
            FAILED_ODEPTH=$((FAILED_ODEPTH + 1))
            FAILED_BY_PROFILE["$profile"]=$((${FAILED_BY_PROFILE["$profile"]:-0} + 1))
            echo "FAIL (oDepth): $name"
        else
            PASSED=$((PASSED + 1))
        fi
    else
        FAILED=$((FAILED + 1))
        FAILED_OTHER=$((FAILED_OTHER + 1))
        FAILED_BY_PROFILE["$profile"]=$((${FAILED_BY_PROFILE["$profile"]:-0} + 1))
        echo "FAIL (other): $name - exit code $exit_code"
        echo "  Output: $(echo "$output" | head -3)"
    fi
    
    # Progress
    if [ $TOTAL -eq 50 ] || [ $TOTAL -eq 100 ] || [ $TOTAL -eq 200 ] || [ $TOTAL -eq 300 ] || [ $TOTAL -eq 400 ] || [ $TOTAL -eq 500 ] || [ $TOTAL -eq 591 ]; then
        echo "  Progress: $TOTAL/591 (passed: $PASSED, failed: $FAILED, oDepth: $FAILED_ODEPTH)"
    fi
done

echo ""
echo "=== Coverage Report ==="
echo "Total shaders: $TOTAL"
echo "Passed: $PASSED"
echo "Failed: $FAILED"
echo "  oDepth failures: $FAILED_ODEPTH"
echo "  Other failures: $FAILED_OTHER"
echo ""
echo "Failures by profile:"
for profile in $(printf '%s\n' "${!FAILED_BY_PROFILE[@]}" | sort); do
    echo "  $profile: ${FAILED_BY_PROFILE[$profile]}"
done

# List oDepth failures
echo ""
echo "=== oDepth Failure Details (first 10) ==="
count=0
for blob in "$SHADER_DIR"/*.bin; do
    name=$(basename "$blob")
    output=$("$SHADER2WGSL" "$blob" 2>&1)
    exit_code=$?
    
    if [ $exit_code -eq 0 ] && echo "$output" | grep -qi "odepth\|oDepth"; then
        echo "$name"
        count=$((count + 1))
        if [ $count -ge 10 ]; then
            echo "  ... and $((FAILED_ODEPTH - 10)) more"
            break
        fi
    fi
done

if [ $FAILED_ODEPTH -gt 0 ]; then
    echo ""
    echo "=== oDepth Failure Summary ==="
    echo "Total oDepth failures: $FAILED_ODEPTH"
fi
#!/bin/bash
# Build script for the D3D9 HLE shader-capture test (native, no WASM).
# Pass a shader directory as the first argument to also exercise real blobs:
#   tests/build_d3d9_shader_capture.sh /tmp/opencode/ps-shaders

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
RUNTIME_DIR="$PROJECT_ROOT/lifter/runtime"
TEST_DIR="$SCRIPT_DIR"

echo "=== Building D3D9 HLE Shader Capture Test ==="

BUILD_DIR="$TEST_DIR/build"
mkdir -p "$BUILD_DIR"
OUTPUT="$BUILD_DIR/d3d9_shader_capture"

SOURCES=(
    "$TEST_DIR/d3d9_shader_capture.c"
    "$RUNTIME_DIR/d3d9_hle.c"
)

CFLAGS=(
    -std=c11
    -Wall -Wextra
    -O2
    -g
    "-I$RUNTIME_DIR"
    "-I$TEST_DIR"
    -DD3D9_HLE_STANDALONE=1
)

echo "Compiling..."
gcc "${CFLAGS[@]}" "${SOURCES[@]}" -o "$OUTPUT"

echo "Build successful: $OUTPUT"
echo ""
echo "=== Running test ==="
"$OUTPUT" "$@"

echo ""
echo "=== Test completed successfully ==="

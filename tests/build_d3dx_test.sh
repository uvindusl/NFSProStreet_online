#!/bin/bash
# Build script for D3DX9 HLE tests (native, no WASM)

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
RUNTIME_DIR="$PROJECT_ROOT/lifter/runtime"
TEST_DIR="$SCRIPT_DIR"

echo "=== Building D3DX9 Tests ==="
echo "Project root: $PROJECT_ROOT"
echo "Runtime dir: $RUNTIME_DIR"
echo "Test dir: $TEST_DIR"

# Build output
BUILD_DIR="$TEST_DIR/build"
mkdir -p "$BUILD_DIR"

OUTPUT="$BUILD_DIR/d3dx_test"

# Source files
SOURCES=(
    "$TEST_DIR/d3dx_test.c"
    "$RUNTIME_DIR/d3dx9_hle.c"
    "$RUNTIME_DIR/d3d9_hle.c"
)

# Include directories
INCLUDES=(
    "-I$RUNTIME_DIR"
    "-I$TEST_DIR"
)

# Compile flags
CFLAGS=(
    -std=c11
    -Wall -Wextra
    -O2
    -g
    "${INCLUDES[@]}"
    -DD3D9_HLE_STANDALONE=1
    -lm
)

echo "Compiling..."
gcc "${CFLAGS[@]}" "${SOURCES[@]}" -o "$OUTPUT"

echo "Build successful: $OUTPUT"
echo ""
echo "=== Running test ==="
"$OUTPUT"

echo ""
echo "=== Test completed successfully ==="
#!/bin/bash
# Build script for runtime lifecycle & pump tests

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
TEST_DIR="$SCRIPT_DIR"

echo "=== Building Runtime Lifecycle Tests ==="

BUILD_DIR="$TEST_DIR/build"
mkdir -p "$BUILD_DIR"

OUTPUT="$BUILD_DIR/runtime_lifecycle_test"

CFLAGS=(
    -std=c11
    -Wall -Wextra
    -O2
    -g
    -I"$PROJECT_ROOT/lifter/runtime"
    -lpthread
)

echo "Compiling..."
gcc "${CFLAGS[@]}" "$TEST_DIR/runtime_lifecycle_test.c" -o "$OUTPUT"

echo "Build successful: $OUTPUT"
echo ""
echo "=== Running test ==="
"$OUTPUT"

echo ""
echo "=== Test completed successfully ==="

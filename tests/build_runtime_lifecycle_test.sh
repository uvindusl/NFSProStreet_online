#!/bin/bash
# Build script for runtime lifecycle & pump tests (native execution)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$SCRIPT_DIR/build"
mkdir -p "$BUILD_DIR"
OUTPUT="$BUILD_DIR/runtime_lifecycle_test"

echo "=== Building Runtime Lifecycle Tests ==="

gcc -std=c11 -Wall -Wextra -Werror \
    -Wno-unused-parameter -Wno-unused-variable -Wno-unused-but-set-variable -Wno-format-truncation \
    -fno-strict-aliasing -fwrapv -fno-stack-protector \
    -D_POSIX_C_SOURCE=200809L \
    -I"$PROJECT_ROOT/lifter/runtime" \
    "$SCRIPT_DIR/runtime_lifecycle_test.c" \
    -o "$OUTPUT" \
    -lm -lpthread

echo "Build successful: $OUTPUT"
echo ""
echo "=== Running test ==="
"$OUTPUT"
echo ""
echo "=== Test completed successfully ==="

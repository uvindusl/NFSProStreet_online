#!/bin/bash
# Build script for shader_disasm (native, plain gcc)

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
TOOLS_DIR="$SCRIPT_DIR"
VSEMU_DIR="$PROJECT_ROOT/../web/runtime/web/runtime"
VSEMU_WEB_DIR="$PROJECT_ROOT/../web/runtime/web"

echo "=== Building Shader Disassembler ==="
echo "Project root: $PROJECT_ROOT"
echo "Tools dir: $TOOLS_DIR"
echo "VSEMU dir: $VSEMU_DIR"

BUILD_DIR="$TOOLS_DIR/build"
mkdir -p "$BUILD_DIR"

OUTPUT="$BUILD_DIR/shader_disasm"

# Source files
SOURCES=(
    "$TOOLS_DIR/shader_disasm.c"
    "$VSEMU_DIR/vsemu.c"
)

# Include directories
INCLUDES=(
    "-I$VSEMU_DIR"
    "-I$VSEMU_WEB_DIR"
    "-I$TOOLS_DIR"
)

# Compile flags
CFLAGS=(
    -std=c11
    -Wall -Wextra
    -O2
    -g
    "${INCLUDES[@]}"
    -DVSEMU_WGSL=1
    -lm
)

echo "Compiling..."
gcc "${CFLAGS[@]}" "${SOURCES[@]}" -o "$OUTPUT"

echo "Build successful: $OUTPUT"
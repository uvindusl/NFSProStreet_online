#!/usr/bin/env bash
# Build and run the synthetic CPU shader semantics test.  An optional first
# argument replaces vsemu.c, so the unit test can compile deliberately broken
# copies to prove the assertions catch regressions.
set -euo pipefail

HERE=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
ROOT=$(cd -- "$HERE/../.." && pwd)
BUILD="$ROOT/out/tools"
mkdir -p "$BUILD"

read -r -a CC_CMD <<< "${CC:-cc}"
if ! command -v "${CC_CMD[0]}" >/dev/null 2>&1; then
    echo "cpu_shader_semantics_test: no C compiler (${CC_CMD[0]}); set CC=<compiler>" >&2
    exit 77
fi

VSEMU=${1:-$ROOT/web/runtime/web/runtime/vsemu.c}
"${CC_CMD[@]}" -O2 \
    -I "$ROOT/web/runtime" \
    -I "$ROOT/web/runtime/web" \
    -I "$ROOT/web/runtime/web/runtime" \
    -o "$BUILD/cpu_shader_semantics_test" \
    "$HERE/cpu_shader_semantics_test.c" \
    "$VSEMU" \
    -lm

exec "$BUILD/cpu_shader_semantics_test"

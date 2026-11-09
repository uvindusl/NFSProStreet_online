#!/bin/bash
# Build the local ProStreet lift as a diagnostic browser WebAssembly module.
#
# The executable and lift are deliberately inputs, not build outputs:
#   game/nfsps.exe       the user's copy, also proves the lift's identity
#   work/gen/*.c         generated lift
#   work/impargs.c       generated import table
#
# Set WASI to a wasi-sdk directory, or use a wasi-sdk-* directory under deps/.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

WASI="${WASI:-}"
if [ -z "$WASI" ]; then
  for candidate in "$ROOT"/deps/wasi-sdk-*; do
    [ -x "$candidate/bin/clang" ] || continue
    WASI="$candidate"
  done
fi

die() {
  echo "web/build.sh: $*" >&2
  exit 1
}

[ -f game/nfsps.exe ] ||
  die "game/nfsps.exe is missing; provide your own copy before building"
[ -f work/gen/decls.h ] || die "work/gen is missing; run ./scripts/lift.sh first"
[ -f work/gen/fntab.c ] || die "work/gen/fntab.c is missing; run ./scripts/lift.sh first"
[ -f work/impargs.c ] || die "work/impargs.c is missing; run ./scripts/lift.sh first"
compgen -G 'work/gen/shard_*.c' >/dev/null ||
  die "work/gen contains no lifted shards; run ./scripts/lift.sh first"
[ -x "$WASI/bin/clang" ] ||
  die "wasi-sdk not found; set WASI=/path/to/wasi-sdk"
[ -x "$WASI/bin/wasm-ld" ] ||
  die "wasi-sdk linker not found: $WASI/bin/wasm-ld"

CC="$WASI/bin/clang"
command -v "$CC" >/dev/null || die "cannot execute $CC"

OUT="$ROOT/out/web"
OBJ="$OUT/obj"
mkdir -p "$OBJ"

TARGET="--target=wasm32-wasip1-threads -pthread"
OPT="${OPT:--O2}"
CFLAGS="$TARGET $OPT -msimd128 -mbulk-memory -mnontrapping-fptoint \
  -fno-delete-null-pointer-checks -Iruntime -Iwork/gen"

echo "web/build.sh: compiling lifted shards"
for src in work/gen/shard_*.c work/gen/fntab.c work/impargs.c; do
  obj="$OBJ/$(basename "$src" .c).o"
  "$CC" $CFLAGS -Wno-unused-label -Wno-parentheses-equality \
    -c "$src" -o "$obj"
done

echo "web/build.sh: compiling browser host"
"$CC" $CFLAGS -c web/host.c -o "$OBJ/host.o"
"$CC" $CFLAGS -c runtime/s2_import_dispatch.c -o "$OBJ/s2_import_dispatch.o"

echo "web/build.sh: linking diagnostic module"
"$CC" $TARGET $OPT -o "$OUT/prostreet.wasm" \
  "$OBJ"/*.o \
  -Wl,--no-entry -Wl,--export-memory \
  -Wl,--export=prostreet_diagnostic \
  -Wl,--export=prostreet_status \
  -Wl,--initial-memory=67108864 -Wl,--max-memory=2147483648 \
  -Wl,-z,stack-size=1048576 \
  -lm

SIZE="$(wc -c < "$OUT/prostreet.wasm" | tr -d ' ')"
printf '{"size":%s,"opt":"%s"}\n' "$SIZE" "$OPT" \
  > "$OUT/prostreet.json"
echo "web/build.sh: link OK: $OUT/prostreet.wasm ($SIZE bytes)"

if [ "${1:-}" = smoke ]; then
  node web/smoke.mjs "$OUT/prostreet.wasm"
fi

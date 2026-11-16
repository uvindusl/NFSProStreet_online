#!/bin/bash
# Build the game module for the browser: out/web/nfsu2.wasm (+ nfsu2.json).
#
#   web/build.sh            lifted shards (incremental) + runtime + link
#   FULL=1 web/build.sh     recompile everything
#   OPT=-O2 web/build.sh    another optimisation level (default -O3)
#
# Needs gen/all and gen/server (web/gen.sh <NFSPROSTREET.EXE>: ./gen.sh with the web's lift
# options; it lifts server.dll too) and wasi-sdk in deps/ (see docs/web).
# Toolchain: wasi-sdk (clang + wasm-ld), target wasm32-wasip1-threads, run by
# the page's own host (web/src/host): no Emscripten.
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"; cd "$ROOT"
WASI="${WASI:-$(ls -d "$ROOT"/deps/wasi-sdk-*-x86_64-linux "$ROOT"/../reference/nfsu2-web/deps/wasi-sdk-*-x86_64-linux 2>/dev/null | tail -1)}"
[ -x "$WASI/bin/clang" ] || { echo "wasi-sdk not found in deps/ (set WASI=...)"; exit 1; }
# guest memory accesses must keep their may_alias types (cpu.h)
WASI="$WASI" web/tools/aliasguard.sh > /dev/null || { WASI="$WASI" web/tools/aliasguard.sh; exit 1; }
# a worktree's gen links: to the lift of this checkout's sources made before, if there is one (web/tools/gencache.sh)
web/tools/gencache.sh link
[ -f gen/all/fntab.c ] || { echo "gen/all missing: run web/gen.sh <path to NFSPROSTREET.EXE>"; exit 1; }
[ -f gen/server/fntab.c ] || { echo "gen/server missing: run web/gen.sh <path to NFSPROSTREET.EXE> (it lifts server.dll too)"; exit 1; }
# The lift must be the one this runtime needs: its _orig declarations, and
# gen.sh's stamp (override list, hook list, lifter; web/tools/gencheck.sh)
web/tools/gencheck.sh
LIFT="$(cat gen/all/web-lift.txt 2>/dev/null || echo unknown)"
[ "$LIFT" != unknown ] || echo "note: gen/all was not made by web/gen.sh (lift options unknown; web/gen.sh turns on FLAGABI and X87OPT)"
CC="$WASI/bin/clang"; OBJDUMP="$WASI/bin/llvm-objdump"
J="${J:-$(sysctl -n hw.ncpu 2>/dev/null || nproc)}"
OPT="${OPT:--O3}"
OUT="$ROOT/out/web"; mkdir -p "$OUT/obj" "$OUT/obj/srv" "$OUT/rt"
OBJCACHE="${OBJCACHE:-$HOME/.cache/nfsps-web/obj}"; mkdir -p "$OBJCACHE"

# The guest owns [0, GUEST_END) of linear memory (runtime/cpu.h); the
# runtime's data starts there.  The page gives the game INITIAL_MB at start
# (nfsu2.json "pages"): the guest span plus room for the runtime heap; it can
# grow to 4 GB.  The module itself declares only MIN_MB, just past the
# runtime's data (~30 MB) and main stack (8 MB): the runtime heap then takes
# all its memory through runtime/web/sbrk.c, which keeps it one run however
# much the page gave, and the [status] line's brk= says how much of the memory
# the game has used (~930 MB with the renderer in the menus and a race).
GUEST_END=671088640   # 0x28000000
INITIAL_MB="${INITIAL_MB:-1024}"
MIN_MB="${MIN_MB:-704}"
TARGET="--target=wasm32-wasip1-threads -pthread"
FEAT="-msimd128 -mbulk-memory -mnontrapping-fptoint -mtail-call -mexception-handling -mllvm -wasm-enable-sjlj -mllvm -wasm-use-legacy-eh=false"
DEFS="-D_WASI_EMULATED_MMAN -D_WASI_EMULATED_SIGNAL -D_WASI_EMULATED_PROCESS_CLOCKS"
# -fno-delete-null-pointer-checks: guest address 0 is real wasm memory (cpu.h)
CFLAGS="$TARGET $OPT $FEAT $DEFS -fno-delete-null-pointer-checks -include web/runtime/web/compat/wasm_plat.h -Iweb/runtime/web/compat -Ilifter/runtime -Igen/all"
# the lifted shards only: guest memory as an object of its own (cpu.h, NFSU2_GUEST_OBJ)
SHARDFLAGS="-DNFSU2_GUEST_OBJ"
export CC OBJDUMP CFLAGS SHARDFLAGS OUT FULL MEMCHECK OBJCACHE
HDRSUM="$( (cat lifter/runtime/cpu.h lifter/runtime/splitscreen_mem.h web/runtime/web/compat/wasm_plat.h; echo "$CFLAGS $SHARDFLAGS") | shasum | cut -c1-16)"; export HDRSUM

# ---- lifted shards (content-hash incremental, as build.sh) ----
# gen/all (NFSPROSTREET.EXE) -> out/web/obj, gen/server (server.dll, the LAN server
# lifted as a second module, see gen.sh) -> out/web/obj/srv: one pool, the same
# rule for both.  A shard's key is its source, its own module's decls.h and the
# flags, so the two modules never share cache entries by accident.
{ (cd gen/all && ls shard_*.c fntab.c impargs.c $( [ -f stubs.c ] && echo stubs.c ) | sed "s|^|all |")
  (cd gen/server && ls shard_*.c fntab.c impargs.c modinfo.c $( [ -f stubs.c ] && echo stubs.c ) | sed "s|^|server |"); } |
  xargs -P "$J" -n 2 sh -c '
  case "$1" in all) od="$OUT/obj" ;; *) od="$OUT/obj/srv" ;; esac
  src="gen/$1/$2"; obj="$od/$(basename "$2" .c).o"
  sum=$( (cat "$src" "gen/$1/decls.h"; echo "$HDRSUM") | shasum | cut -c1-40)
  if [ -z "$FULL" ] && [ -f "$obj" ] && [ "$(cat "$obj.sum" 2>/dev/null)" = "$sum" ]; then exit 0; fi
  mc=""; case ",$MEMCHECK," in *",$(basename "$2" .c),"*) mc="-DNFSU2_MEMCHECK" ;; esac
  [ -n "$mc" ] && sum="$sum-memchk"
  if [ -z "$FULL" ] && [ -f "$obj" ] && [ "$(cat "$obj.sum" 2>/dev/null)" = "$sum" ]; then exit 0; fi
  # shared, content-addressed cache: every worktree with the same shard, cpu.h
  # and flags reuses one compile
  if [ -z "$FULL" ] && [ -f "$OBJCACHE/$sum.o" ]; then ln -f "$OBJCACHE/$sum.o" "$obj" 2>/dev/null || cp "$OBJCACHE/$sum.o" "$obj"
    echo "$sum" > "$obj.sum"; exit 0; fi
  o=$($CC $CFLAGS $SHARDFLAGS $mc -I"gen/$1" -Wno-unused-label -Wno-parentheses-equality -c -o "$obj" "$src" 2>&1); r=$?
  [ -z "$o" ] || printf "%s\n" "$o" | head -5
  [ $r -eq 0 ] || { echo "FAILED: $src" >&2; exit $r; }
  # a lookup table of guest addresses (g_guest + K in data) links as zeros
  # (cpu.h): compile the shard again without jump tables, which has none
  guestdata() { "$OBJDUMP" -r "$obj" | awk "/^RELOCATION RECORDS FOR \\[DATA\\]/ { d = 1; next } /^RELOCATION RECORDS/ { d = 0 }
    d && \$3 ~ /^g_guest[+-]/ && \$3 != \"g_guest+0\" { f = 1 } END { exit !f }"; }
  if guestdata; then
    o=$($CC $CFLAGS $SHARDFLAGS $mc -I"gen/$1" -DNFSU2_NO_GUEST_DATA -Wno-unused-label -Wno-parentheses-equality -c -o "$obj" "$src" 2>&1); r=$?
    [ -z "$o" ] || printf "%s\n" "$o" | head -5
    [ $r -eq 0 ] || { echo "FAILED: $src (no guest data)" >&2; exit $r; }
  fi
  if [ -z "$FULL" ]; then cp "$obj" "$OBJCACHE/$sum.o" 2>/dev/null || true; fi
  echo "$sum" > "$obj.sum"
  ' _ || exit 1

# ---- runtime ----
# The browser runtime files (web/compat, web/*.c) first, then the shared
# runtime (lifter/runtime/*.c except the native backends).
RTFILES=$(ls runtime/web/*.c | grep -v "_web.c$" | sort)
RTFILES="$RTFILES runtime/web/compat/wasm_plat.h"
RTFILES="$RTFILES lifter/runtime/cpu.c lifter/runtime/guest.c lifter/runtime/setting.c lifter/runtime/featurelist.c lifter/runtime/features.c lifter/runtime/splitscreen.c lifter/runtime/gh_alloc.c lifter/runtime/threadpool.c"
RTFILES="$RTFILES runtime/web/main_web.c runtime/web/sbrk.c runtime/web/gpu_web.c runtime/web/audio_web.c runtime/web/input_web.c runtime/web/net_lan.c runtime/web/status_web.c runtime/web/thread_exit.c runtime/web/tz.c runtime/web/stackprobe.c runtime/web/memcmp.c runtime/web/sha256.c"
RTFILES="$RTFILES runtime/web/inflate.c runtime/web/roam_web.c runtime/web/texrep_web.c runtime/web/radio_proto.c runtime/web/photo_icon.c runtime/web/null_platform.c"
for f in $RTFILES; do
  [ -f "$f" ] || { echo "missing: $f" >&2; exit 1; }
done

# compile runtime files
for src in $RTFILES; do
  obj="$OUT/rt/$(basename "$src" .c).o"
  sum=$( (cat "$src"; echo "$HDRSUM") | shasum | cut -c1-40)
  if [ -z "$FULL" ] && [ -f "$obj" ] && [ "$(cat "$obj.sum" 2>/dev/null)" = "$sum" ]; then continue; fi
  $CC $CFLAGS -Iruntime -Iruntime/web/compat -c -o "$obj" "$src" 2>&1 | head -5
  echo "$sum" > "$obj.sum"
done

# ---- link ----
# Main module (NFSPROSTREET.EXE)
OBJS_ALL=$(ls "$OUT/obj"/*.o 2>/dev/null | sort)
OBJS_RT=$(ls "$OUT/rt"/*.o 2>/dev/null | sort)
WASM="$OUT/nfsu2.wasm"
WASM_JSON="$OUT/nfsu2.json"
$CC $CFLAGS -Wl,--export=memory -Wl,--export=__heap_base -Wl,--export=__data_end \
  -Wl,--initial-memory=$((INITIAL_MB*1024*1024)) -Wl,--max-memory=4294967296 \
  -Wl,--stack-first -Wl,--stack=8388608 \
  -Wl,--allow-undefined -Wl,--no-entry \
  -o "$WASM" $OBJS_ALL $OBJS_RT 2>&1 | head -10

# Server module (server.dll)
OBJS_SRV=$(ls "$OUT/obj/srv"/*.o 2>/dev/null | sort)
WASM_SRV="$OUT/server.wasm"
$CC $CFLAGS -Wl,--export=memory -Wl,--export=__heap_base -Wl,--export=__data_end \
  -Wl,--initial-memory=$((INITIAL_MB*1024*1024)) -Wl,--max-memory=4294967296 \
  -Wl,--stack-first -Wl,--stack=8388608 \
  -Wl,--allow-undefined -Wl,--no-entry \
  -o "$WASM_SRV" $OBJS_SRV $OBJS_RT 2>&1 | head -10

# ---- generate nfsu2.json (page manifest) ----
cat > "$WASM_JSON" <<EOF
{
  "wasm": "nfsu2.wasm",
  "wasmServer": "server.wasm",
  "pages": $((INITIAL_MB * 1024 * 1024 / 65536)),
  "guestEnd": $GUEST_END,
  "minPages": $((MIN_MB * 1024 * 1024 / 65536)),
  "initialMemoryMB": $INITIAL_MB,
  "minMemoryMB": $MIN_MB
}
EOF

echo "Built $WASM and $WASM_SRV"
echo "Manifest: $WASM_JSON"
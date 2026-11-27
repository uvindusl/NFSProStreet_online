#!/bin/bash
# Generate the boot lift: every function the runtime needs, from the seeds in
# lifter/seeds-boot.txt.
#
#   scripts/lift_boot.sh [pe-image] [shards]
#
# Seeds: scripts/boot_seeds.py.  It starts from the full-map function list and
# adds the targets a static call graph cannot see: SecuROM's `jmp [diabolus
# slot]` thunks, and the continuation addresses of its `push ret; push target;
# ret` obfuscated calls.  Both are needed before the CRT's static constructors
# run (the first stops were 0x52ea80 and 0x1a11e20, the allocator's init).
#
# JTSWITCH=1 is required.  Without it a `jmp dword [reg*4+table]` (MSVC switch)
# is a dispatcher tail call, and the table targets are never lifted: the first
# switch the guest reaches stops on an unresolved indirect call (0x464f2d).
# With it each table becomes a C switch inside its function.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
GEN="${GEN:-$ROOT/out/lift-boot}"
PE="${1:-artifacts/nfs.exe.unpacked}"
NSHARD="${2:-64}"

cd "$ROOT"
python3 scripts/boot_seeds.py "$PE" lifter/seeds-boot.txt
# Instruction hooks: every HOOK_SITE(0x...) in the runtime sources; the
# lifted code calls guest_hook() before each of those instructions.
HOOKS="$( { grep -ohE 'HOOK_SITE\(0x00[0-9a-f]{6}\)' lifter/runtime/*.c || true; } | sed -E 's/HOOK_SITE\((0x[0-9a-f]+)\)/\1/' | sort -u | paste -sd, -)"
GENOUT="$GEN" HOOKS="$HOOKS" SEEDS=lifter/seeds-boot.txt RELOCS=lifter/relocs-empty.txt JTSWITCH=1 PUSHRET=1 CALLCHK=${CALLCHK:-1} \
    python3 lifter/tools/genall.py "$PE" "$NSHARD"

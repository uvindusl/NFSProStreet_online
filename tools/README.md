# tools/ — lifter

`lift.py`, `genall.py`, `lregs.py` and `retpast.txt` are a fork of the
[x86→C lifter from nfsu2-web](https://github.com/th0mk/nfsu2-web) (MIT, see
`LICENSE.nfsu2-web-MIT`), pinned at commit
`9e0230212776f6c9a4d0db30c1963e9730138553`. `split_reloc.py` is rewritten.

The upstream lifter is what nfsu2-web uses to turn Underground 2's `.text`
into C. It is not game-specific in principle, but it was only ever exercised
on that one binary, so this fork carries the fixes ProStreet needed.

## Usage

`scripts/lift.sh` drives everything:

```
scripts/lift.sh [path/to/nfsps.exe]
```

1. `scripts/seed_scan.py` — every dword in a non-executable section that
   points into `.text` is a seed (the image has **no base relocations**, so
   the pointers are still absolute file values).
2. `scripts/curate_seeds.py` — reports how many seeds look like data;
   informational only, every seed is lifted.
3. `tools/genall.py` — discovers callees and jump tables, then lifts all
   functions into `work/gen/shard_*.c` (+ `decls.h`, `fntab.c`).

The output is derived from the game executable: it is gitignored and must
never be committed or redistributed, exactly as in nfsu2-web.

## Differences from upstream

**`split_reloc.py` is rewritten.** Upstream rewrites operands at a handful of
hard-coded Underground 2 instruction addresses (split-screen player tables)
into runtime symbol accesses. ProStreet is also based at `0x400000`, so those
addresses match *different* ProStreet instructions and the rewrites would fire
silently on them. The fork keeps only the plain `0x...u` rendering. Add
ProStreet-specific rewrites here, keyed by instruction address, if a guest
access ever needs to be redirected at lift time.

**`lift.py`:**

| Fix | Symptom on ProStreet |
| --- | --- |
| strip the `lock` prefix from the capstone mnemonic (`lock xadd`, `lock cmpxchg`, …) | unsupported instructions in the game's interlocked paths; no handler sees `"lock xadd"` |
| `cmpxchg8b` | the exe's 64-bit interlocked helpers |
| `pshufd` with a **memory** source | 8 traps in `sub_008efe80`, the SSE2 sites in the game's own code; upstream only handled the register form |
| `fstpnce` treated as `fstp` | x87 stores in the float paths |
| `repz ret` treated as `ret` | MSVC's aligned-return padding (`rep ret`) |
| `retpast.txt` emptied | upstream's list is Underground 2 callee addresses; wrong addresses here would corrupt returns |

The `lock` prefix strip is safe because the runtime executes guest threads
under one lock, so a plain read-modify-write already has the ordering the
guest expects.

`lregs.py` (register-state simplification pass, `LREGS=1`) is carried over
unchanged.

## Verification

`scripts/audit_lift.py` aggregates the `unsupported_insn` stubs the lifter
emits, and `scripts/host_compile_check.sh` compiles every shard with clang.
A trap is a site that will abort at runtime; a compile error is a lifter bug.

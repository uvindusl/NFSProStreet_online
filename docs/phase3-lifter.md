# Phase 3 — The C lifter

Target: `artifacts/nfs.exe.unpacked` — the same runtime image Phase 2 maps
(SHA-256 `805bf77c…62fd962a`). Phase 3 turns the reachable part of that image
into C and proves the whole set builds.

The plan's Phase 3 is the recompiler itself. This record covers the first half
of the agreed approach — **lift the reachable set, prove it compiles, then
extend to the full map** — and ends with the gate that says the reachable set
is done.

## 1. What Phase 3 does

Phase 2 produced a function map. Phase 3 turns each function into a C body with
the same 32-bit x86 semantics, under a fixed ABI:

```
void sub_00428c25(CPU *c);
```

- Guest registers live in a `CPU` struct (`c->eax`, `c->esp`, …), flags are
  separate 0/1 fields, x87 and XMM state are modelled.
- Guest memory is a flat 32-bit address space behind `MR8/MR16/MR32/MR64/MRF32`.
- A direct `call` becomes `PUSH32(retaddr); sub_XXXXXXXX(c);`.
- An indirect `call`/`jmp` becomes `DISPATCH_CALL` / `DISPATCH_TAIL`, which
  consult a dispatch table and otherwise fall back to the runtime dispatcher.
- A direct branch inside `.text` becomes `goto L_XXXXXXXX;`.
- An instruction the lifter cannot express becomes `unsupported_insn(c, ip,
  "reason")` — a loud trap, never silently wrong code.

## 2. Toolchain

The validated `nfsu2-web` lifter is reused rather than rewritten. Its `lift.py`
already implements the instruction set, the MSVC switch-table recogniser, the
x87 model and the flag ABI; re-deriving that for ProStreet would be months of
work for no new coverage.

| file | role |
|---|---|
| `lifter/tools/lift.py` | x86-32 → C, one function at a time (1,970 lines) |
| `lifter/tools/genall.py` | discovers the function set, lifts in parallel, writes shards + `fntab.c` |
| `lifter/tools/lregs.py` | register-local optimisation (`LREGS=1`, off for this gate) |
| `lifter/tools/flagabi.py` | flag-liveness pass (`FLAGABI=`, off here) |
| `lifter/tools/split_reloc.py` | operand splitter used by `lift.py` |
| `lifter/tools/retpast.txt` | callees that return past their caller; **empty** for ProStreet |

Source: `reference/nfsu2-web`, commit `9e02302` (2026-11-09).

Inputs:

| input | value |
|---|---|
| `SEEDS` | `lifter/seeds-reachable.txt` — 28,206 addresses (seeds + direct-call closure + entry) |
| `RELOCS` | `lifter/relocs-empty.txt` — the image has no base-relocation table |
| `JTSWITCH` | off (Phase 2 already showed the JTSWITCH drop removes nothing here) |
| `LREGS` / `FLAGABI` | off |

Four small fixes were made to the reused lifter while lifting ProStreet; they
are listed in §6.

## 3. What was generated

```bash
GENOUT=out/lift-reachable SEEDS=lifter/seeds-reachable.txt \
  RELOCS=lifter/relocs-empty.txt \
  python3 lifter/tools/genall.py artifacts/nfs.exe.unpacked 64
```

| metric | value |
|---|---|
| Functions lifted | **28,385** |
| Shards | 66 (`shard_0000..0065.c`) |
| C source | **99.0 MB**, 2,355,672 lines |
| Guest instructions represented | 1,557,792 |
| Largest function | `sub_006b6d70` — 8,254 instructions, 102 blocks |
| Truncated branch targets | **0** |
| Unsupported-instruction sites | **270** in 29 functions |

`genall.py` runs its own closure on top of the seeds: 28,206 seeds → 28,375
discovered → 28,385 after the closure shards. That is why the output is a
superset of the analyzer's reachable set (§5).

Each function is emitted twice: an `always_inline` body (`inl_XXXXXXXX`) and a
thin non-inline wrapper (`sub_XXXXXXXX`) that the function table points at.
`fntab.c` carries the address/pointer pair for the runtime dispatcher.

## 4. The compile gate

```bash
scripts/build_lift_gate.sh          # FULL=1 to ignore cached objects
```

It compiles every shard with `gcc -O1 -w`, compiles `fntab.c`, links a driver
that prints `g_fn_count`, runs it, and then runs `scripts/verify_lift.py`.

| | |
|---|---|
| Compiler | gcc 16.2.1, x86-64 host |
| Runtime contract | `lifter/runtime/cpu.h` + `splitscreen_mem.h` (copied from the reference) |
| Shards compiled | **66 / 66** |
| Wall time | **4 min 58 s** on 8 cores (`-O1`, cold) |
| Objects | 69 MB |
| Linked binary | 46 MB |
| Runtime output | `lifted functions: 28385` |

`lifter/runtime/stubs.c` supplies `g_mem`, `g_dtab`, the dispatcher entry
points, `unsupported_insn`, `cpuid_shim`, `rdtsc_shim` and the two splitscreen
helpers. They are deliberately inert: the gate proves the C is well formed and
every symbol resolves, not that the program runs. `lifter/runtime/main.c` is the
six-line driver.

`scripts/verify_lift.py` checks:

| check | result |
|---|---|
| `g_fn_count` matches the address table | 28,385 |
| `g_fn_count` matches the pointer table | 28,385 |
| no duplicate entries | pass |
| every entry has a defined function body | 28,385 / 28,385 |
| every reachable address present | 28,206 / 28,206 |
| closure extras inside `.text` | 2, both real code |
| trap inventory | 270 sites, 29 functions |

## 5. Coverage reconciliation

| set | count |
|---|---|
| Phase 2 reachable (seed + call + entry) | 28,206 |
| reached by the lifter's own closure but classified `recovered`/`imm` by Phase 2 | 177 |
| found only by the lifter's closure | 2 |
| **`fntab.c` entries** | **28,385** |

The two lifter-only entries are `0x00828746` and `0x008ffdf0`. Both are direct
`call` targets inside `.text`; the analyzer's coverage stopped short of the
call sites (`0x0090c1be`, `0x00900321`), so it never queued them. The lifter
decodes those paths, which is the expected direction of disagreement: the
lifter's closure is a superset of the analyzer's.

Nothing outside `.text` was invented.

## 6. Traps

An "unsupported instruction" is emitted as a call to `unsupported_insn(c, ip,
reason)` followed by a return. It is a deliberate abort point, not a wrong
translation.

| reason | sites | what it is |
|---|---|---|
| `lcall` | 207 | far call (16:32) — flat-model Windows code does not use it |
| `psrldq` | 14 | SSE2 byte-shift right |
| `punpcklqdq` / `punpckhqdq` | 17 | SSE2 unpack |
| `pshufd` | 8 | SSE2 dword shuffle |
| `les` | 5 | load far pointer (16-bit era) |
| `das` | 4 | BCD adjust |
| `far indirect jmp` | 2 | `jmp far [mem]` |
| `fstpnce` | 2 | x87 conditional store |
| `ljmp`, `iretd`, `sldt`, `daa`, `aaa`, `aas`, `aam`, `salc`, `int`, `int1`, `base register 8` | 11 | assorted legacy/undecodable forms |
| **total** | **270** in **29** functions | |

### Almost all of them are one false seed

`0x0091f320` alone contains **202** of the 270 sites, and it is not a function:
its bytes are a table of 16-byte records

```
1c3a9a00 00000000 0c000080 00000000
1c3a9a00 01000000 0c010080 00000000
...
```

and `.rdata` holds a pointer to it at `0x009ec604`. Phase 2's seed scan trusts
every 16-byte-aligned dword in a data section, so a pointer to a data table in
`.text` becomes a "function" whose bytes decode to `lcall`/`aaa`/`das`. It is
recorded here as a known false seed; a data-seed filter belongs to the full-map
pass, not to this gate.

Two smaller ones are also data: `0x00808080` begins with two `.text` pointers
then `int3` padding before real code, and `0x00887800` is reached one byte
before a valid instruction.

### Reachability of the traps

Following the direct-call graph from `_WinMainCRTStartup` (`0x828c25`):

| | |
|---|---|
| lifted functions | 28,385 |
| reachable by direct calls | **1,799** |
| functions containing traps | 29 |
| **reachable functions containing traps** | **0** |

The 26,586 functions not reached this way are seeds (vtables, callbacks,
import thunks) that no static direct call touches. The count is a lower bound,
not a claim that they are dead.

### Lifter fixes made here

| issue | fix | effect |
|---|---|---|
| `jmp far [mem]` (operand size 6) raised `KeyError: 6` | emit a `far indirect jmp` trap | 2 `lift-error` stubs became real bodies |
| `repz ret` was not a terminator, so blocks ran past the return | recognise `ret`/`retf` by last mnemonic token | 1 reachable trap removed; no more decode past a return |
| Capstone folds `lock` into the mnemonic, so `lock xadd` / `lock cmpxchg` / `lock add` missed the existing implementations | strip the `lock ` prefix | 48 traps removed; atomicity is dropped because the port runs the guest on one thread |
| `cmpxchg8b` unimplemented | emit the EDX:EAX compare/store sequence | 6 traps removed |

Before these fixes: 325 trap sites, 20 of them reachable. After: 270 sites,
**0 reachable**. The trap count only fell by 55 because the false seed's 202
`lcall`s remain.

## 7. What this is not

The gate proves compilation, not execution. Still missing, and all Phase 4:

- **Guest memory is not loaded.** `g_mem` is `NULL` in the gate; no lifted body
  is ever called.
- **Imports are not bound.** The 365 names from `out/imports-runtime.json` need
  a Win32/CRT/D3D9 surface before the entry point can run.
- **The dispatch table is sized for NFSU2.** `cpu.h` defines
  `DISPATCH_TEXT_SIZE = 0x381af1`; ProStreet's `.text` is `0x56507b`. Addresses
  past `0x782af1` miss the fast path and fall back to `dispatch_indirect`, which
  is correct but slower. Resize it for ProStreet.
- **`LREGS` is off.** The reference's shipping build used `LREGS=1` for speed;
  the gate uses plain register access.
- **632,463 bytes of `.text` are still unlifted** (Phase 2's gaps), and the
  recovered/immediate entries outside the reachable set are the second half of
  the agreed plan.

## 8. Reproduce

```bash
# 1. the reachable entry list
python3 - <<'EOF'
import json
a = json.load(open("out/analysis.json"))
reach = sorted(int(k,16) for k,v in a["origins"].items() if v in ("seed","call","entry"))
open("lifter/seeds-reachable.txt","w").write("".join(f"{x:08x}\n" for x in reach))
EOF

# 2. lift
GENOUT=out/lift-reachable SEEDS=lifter/seeds-reachable.txt \
  RELOCS=lifter/relocs-empty.txt \
  python3 lifter/tools/genall.py artifacts/nfs.exe.unpacked 64

# 3. compile, link, verify
scripts/build_lift_gate.sh
python3 scripts/lift_traps.py out/lift-reachable out/analysis.json out/lift-traps.json
```

## 9. Phase 3 gate

| Criterion | Status |
|---|---|
| Reachable function set lifted to C | **Pass** — 28,385 functions, 66 shards, 99 MB |
| Whole set compiles | **Pass** — 66/66 shards, gcc 16.2.1, `-O1` |
| Whole set links | **Pass** — 46 MB binary, `g_fn_count` 28,385 |
| Every reachable address is defined | **Pass** — 28,206 / 28,206 |
| No invented entries | **Pass** — 2 lifter-only entries, both real call targets in `.text` |
| Truncated branches | **Pass** — 0 |
| Unsupported instructions are loud | **Pass** — 270 sites, 0 reachable from the entry |
| Runtime semantics proven | **Not in this phase** — Phase 4 |

**Phase 3 (reachable set) is complete.**

## 10. Full map lift (the second half)

The agreed plan was reachable-set first, then the full map. With the reachable
gate green, the same toolchain was run over all 35,853 entries:

```bash
GENOUT=out/lift-full SEEDS=lifter/seeds-full.txt \
  RELOCS=lifter/relocs-empty.txt \
  python3 lifter/tools/genall.py artifacts/nfs.exe.unpacked 64
scripts/build_lift_gate.sh out/lift-full out/analysis.json
```

| metric | reachable | full map |
|---|---|---|
| Functions | 28,385 | **35,857** |
| Shards | 66 | 65 |
| C source | 99.0 MB | **118.9 MB**, 2,813,718 lines |
| Trap sites | 270 | **545 in 207 functions** |
| Traps reachable from entry | 0 | **0** |
| Truncated branches | 0 | **0** |
| Compile + link (8 cores) | 4:58 | **6:07** |
| Linked binary | 46 MB | **55 MB** |

Coverage: all **35,853 / 35,853** map entries are present. The extra 4 are
direct-call targets the lifter found that Phase 2 never queued:
`0x0080d98c`, `0x00828746`, `0x008ffdf0`, `0x009281ae`.

The trap count more than doubles because the full map includes the heuristic
`recovered` and `imm` entries, which are exactly the places where data gets
read as code (`int`, `iretd`, `les`, `aaa`, `ljmp`, …). None of them is
reachable by a direct call from `_WinMainCRTStartup`, so they do not affect the
reachable program; they are the raw material for the data-seed filter in the
next pass.

**Both halves of Phase 3 now compile and link.** The reachable lift
(`out/lift-reachable/`) is the one Phase 4 should run; the full lift
(`out/lift-full/`) is the wider net, with 4 more real functions and 275 more
trap sites that are all unreachable.

## 11. The boot lift — what the runtime actually needs

The full map is not the set the *runtime* needs. Running the guest needs the
functions the boot reaches, and that set is larger than any static call graph
can see. `scripts/lift_boot.sh` builds that set; this section records what it
has to add and why.

```bash
scripts/lift_boot.sh                    # seeds + lift, JTSWITCH=1
# or, step by step:
python3 scripts/boot_seeds.py artifacts/nfs.exe.unpacked lifter/seeds-boot.txt
GENOUT=out/lift-boot SEEDS=lifter/seeds-boot.txt \
  RELOCS=lifter/relocs-empty.txt JTSWITCH=1 \
  python3 lifter/tools/genall.py artifacts/nfs.exe.unpacked 64
```

### 11.1 `JTSWITCH=1` is required

An MSVC switch compiles to `jmp dword ptr [reg*4 + table]`. Without
`JTSWITCH=1` the lifter leaves that to the dispatcher as a tail call, and the
table's targets are then never lifted: 796 tables, 6,046 targets, none of them
in the function set. The first switch the guest reaches stops the boot with an
unresolved indirect call at `0x464f2d`.

With `JTSWITCH=1` each table becomes a C `switch` inside its function and the
targets stay blocks of their parent (`genall.py:drop_table_targets`).

### 11.2 SecuROM thunks into the diabolus section

The image has an RWX section at `0x10ea000..0x1aa8000` (SecuROM's code and
its data). `.text` reaches part of it only through `jmp/call dword ptr [slot]`
where the slot lives in that section and the loader fills it in at run time:
2,153 slots, of which 1,279 point into `.text` and 874 into the diabolus
section itself.

**This section is real code and must be lifted, not skipped.** `0x1a11e20` is
the allocator's initialiser: it zeroes its fields, calls `0x463f70`
(→ `malloc(0x219000)`), stores the buffer at `[esi+0x110]` and sets
`[esi+0x10c] = 1`. Emulating the return instead left `[esi+0x110] = 0`, and
the next allocation wrote through a null pointer.

`scripts/boot_seeds.py` reads the slot values out of the image and seeds
them. It must read the **runtime dump** (`artifacts/nfs.exe.unpacked`): the
retail exe still has the encrypted slots, and they point elsewhere
(`0x19ede0c` is `0x1a11e20` in the dump but `0x1a66130` in the retail exe).

`dispatch.c` keeps a second, address-sorted table for the diabolus section
(16,384 entries) and runs the lifted entry when there is one.

### 11.3 `push ret; push target; ret` is a call

SecuROM obfuscates calls by pushing a continuation address and jumping to the
target, leaving the continuation as the return address:

```
push 0x0040412c      ; where to continue
push 0x00463e90      ; the target
ret
```

Kept with literal stack semantics, the dispatcher needs that continuation as
a function of its own: 1,348 occurrences, 2,289 distinct continuations (the
diabolus section accounts for the rest). Without them the first obfuscated
call lands on an address that is not in the function table.

With `PUSHRET=1` (what `scripts/lift_boot.sh` uses) the lifter emits the call
instead: it runs the target and then, when the target returned to the pushed
address, the continuation (which still returns to the lifted caller). The same
holds for `push A; jmp B`. `CALLCHK=1` adds a check after every call: when a
callee left a different address in `eip` (a SecuROM continuation), the
dispatcher follows it (`PROSTREET_CALLCHK=log|off` changes that at run time).

### 11.4 Function pointers in data tables

A pointer in a data table is invisible to the call graph: nothing `call`s it,
so nothing discovers it. The boot needs them from the first moment — the
CRT's static-constructor arrays are called by pointer before `main` exists:

| table | contents |
|---|---|
| `_initterm_e` at `0x96a1f8..0x96a204` | `0x828b43`, 0, `0x828fee` |
| `_initterm` at `0x96a1e0..0x96a1f8` | `0x959700`, `0x959710`, `0x959740`, `0x959770`, `0x9597a0`, 0 |

The C++ vtables in `.rdata` are the same problem one phase later.

`scripts/boot_seeds.py` therefore seeds every 4-byte-aligned word that points
into executable memory, in three regions:

| region | read from | why |
|---|---|---|
| `.rdata` | the dump | CRT init arrays, vtables |
| `.data` | the **retail** image | the runtime overlays `.data` from it, so those are the pointers the guest reads |
| diabolus | the dump | the slots SecuROM calls through (`mov eax,[slot]; call eax`) |

A word that is not really a pointer only adds an unreachable function, and a
seed that lands inside an already-lifted body does not truncate it: phase 2
lifts every entry independently from its own address.

### 11.5 The image the runtime loads

`artifacts/nfs.exe.unpacked` is a **dump of a running process**, so its
`.data` holds live heap pointers (the allocator's free list among them) and
its `.rsrc` differs entirely from the retail file. `game/nfsps.exe` has the
initial `.data` the game expects, and `.text` is byte-identical.

So the runtime loads the dump for the layout and then overlays **only**
`.data` from the retail exe (`mem_overlay_section()`), never `.rdata` or
`.rsrc`. Without the overlay the allocator faulted at `0x435390`; with it
`_initterm_e` runs and the CRT gets as far as `__getmainargs`.

### 11.6 Build and run

```bash
STRICT=1 scripts/build_runtime.sh        # warnings are errors for the runtime
scripts/run_boot.sh artifacts/nfs.exe.unpacked 60
```

The shards are generated code and compile with `-w`; the hand-written runtime
(`mem`, `dispatch`, `win32`, `impargs`, `boot`) compiles with `-Wall -Wextra`
and `STRICT=1` turns any warning into a build failure. Do not trust a build
made with `-w`.


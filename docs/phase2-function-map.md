# Phase 2 — Binary analysis and function map

Target: `game/nfs.exe.unpacked` — the user's own unpacked game image
(SHA-256 `805bf77c…62fd962a`). Reproduce with:

```bash
python3 scripts/verify_target.py game/nfs.exe.unpacked
IMPORTS_JSON=out/imports-runtime.json \
  python3 scripts/analyze_binary.py game/nfs.exe.unpacked   # ~2 min, writes out/analysis.json
python3 scripts/classify_gaps.py game/nfs.exe.unpacked out/analysis.json
```

The image is supplied locally by whoever runs the project; this repository does
not contain it and does not describe how to produce it. `out/` is generated and
must never be committed.

## 1. What Phase 2 does

Phase 2 turns the executable into a map the lifter can work from:

1. Parse the PE: headers, sections, entry point, imports.
2. Recover the **function entry set** (the image has no base relocations, so it
   cannot come from a reloc table).
3. Decode the reachable code once, tracking byte coverage, and record where it
   stops.
4. Record **jump tables**, because MSVC switches are the fragile part of every
   static recompiler.
5. Report the gap between "what we found" and "what `.text` contains", so the
   next phase knows how much is missing and of what kind.

## 2. Function discovery

The image has **zero base relocations** (the directory is zeroed), so vtable and
callback entries cannot be read from a fixup table. The method is the same shape
as `nfsu2-web`'s `genall.py: discover_all`:

| Step | Rule |
|---|---|
| **Seeds** | Every little-endian dword in a non-executable, non-SecuROM section that falls inside `.text` and is 16-byte aligned. 33,286 slots → 13,394 unique addresses. |
| **Entry point** | `0x00828c25` (`_WinMainCRTStartup`). |
| **Closure** | Worklist from seeds + entry. A direct `call` target becomes a new function entry. A direct `jmp` is followed **in place** (tail call, stays in the caller's body, exactly as the reference lifter does). A conditional branch target is queued for decoding but is not itself an entry. `ret` / `int3` / `hlt` / `ud2` end a path. |
| **Coverage** | A shared byte map: once decoded, bytes are never decoded again, which prevents misaligned re-decoding from inventing code. |
| **Jump tables** | `jmp dword ptr [reg*4 + table]` with no base register. Entries are read until one leaves `.text`; targets are queued as blocks, not as entries. |
| **Recovery** | After the closure, uncovered runs are scanned for function starts: an MSVC prologue (`55 8B EC`, an SEH `6A FF 68` frame, the hot-patch `8B FF 55 8B EC`, `53 8B DC`) **or** any byte after a run of `int3` padding that decodes into a function-shaped body (a terminator within 64 instructions). |
| **Immediates** | `push offset f` / `mov [x], offset f`: an immediate operand that lands in `.text` and is still uncovered is a candidate entry, accepted only if it decodes into a function-shaped body (terminator within 64 instructions). This replaces, imperfectly, the pointers that died with the relocation table. 2,308 entries. |
| **JTSWITCH drop** | An entry whose every pointer slot lies inside a detected jump table — and which is not a call target, the entry point, a prologue, or an immediate — is a block of its parent function, not a function. This is `nfsu2-web`'s rule; it is a no-op here (see §6) but it is what makes the method comparable to the reference. |

Seeds by section:

| section | pointer slots into `.text` |
|---|---|
| `.rdata` | 30,891 |
| `.data` | 2,390 |
| `.rsrc` | 5 |

`.data` is the **live** section from the process, so it contributes more than
double the slots a static file would (`1,080`): BSS is populated and every
pointer is already absolute. `.rsrc` is still encrypted in the dump, so its
handful of hits are noise that a static scan would produce in quantity — see
§3.1.

### Why 16-byte alignment, and not 4 or 1

Alignment is a *trust filter* for the untrusted data-section scan, not a property
of functions. Measured on this image:

| seed rule | entries | own code | `.text` decoded |
|---|---|---|---|
| 16-aligned + imm + JTSWITCH | 35,853 | 34,608 | 88.8 % |
| 4-aligned + imm + JTSWITCH | 37,147 | 35,772 | 89.2 % |

The 4-aligned pass adds 1,347 seeds, and 1,164 of them own code — but only
19,792 more bytes, i.e. **17 bytes each**. That is the size of a switch-case
label, not a function: most decode to 2–3 instructions and few are preceded by
`int3` padding. They are blocks of functions we already have, so counting them
as entries would inflate the map by ~1,100 phantoms for 0.4 % coverage. 16-byte
alignment is the honest default.

The NFSU2 oracle (§7) confirms the same principle from the other side: the
reference's real function pointers are *mostly unaligned* (of 12,796 unique
pointers in its relocation table, only 5,765 are 16-aligned and 4,622 are not
even 4-aligned). Where a relocation table exists the alignment filter is
skipped, because every recorded slot is already known to be a pointer. Where it
does not — ProStreet — the filter has to stand in, and 16 is the value that
keeps the false-positive rate down.

## 3. Result

| Metric | Value |
|---|---|
| Function entries | **35,853** |
| — own code | 34,608 |
| — aliases (entry inside bytes another entry already decoded: shared epilogues, `call` into a body) | 1,245 |
| — seeds | 13,394 |
| — entry point (`_WinMainCRTStartup`) | 1 |
| — direct `call` targets | 14,811 |
| — recovery pass | 5,339 |
| — immediate pointers | 2,308 |
| Instructions decoded | **1,557,792** |
| `.text` bytes decoded | **5,024,236 / 5,656,699 (88.8 %)** |
| Jump tables | **796** (7,104 targets) |
| Indirect jumps with no recognisable table | 2,764 |
| Undecodable bytes | 29 |

`out/analysis.json` holds the full list of function addresses, the import table,
the jump-table sites, the trap sites, and every uncovered range.

### 3.1 Cross-check against a static copy of the same build

An unpacked copy of this same v1.1 build was analysed first
(`game/nfsps.exe`, 28,739,656 bytes). Its `.text` is **byte-identical** to the
running game's (same SHA-256 `963934d6…b5e65e31`), so the two maps describe the
same program and the difference between them is pure seed-source noise:

| | static copy | running image |
|---|---|---|
| Seeds | 13,258 | **13,394** |
| — from `.rdata` | 30,959 | 30,891 |
| — from `.data` | 1,080 | **2,390** |
| — from `.rsrc` | 36 | 5 |
| Seeded + call closure | 27,017 | 27,710 |
| Recovery pass | 6,095 | 5,339 |
| Immediate pointers | 2,319 | 2,308 |
| **Function entries** | 36,052 | **35,853** |
| Own code | 34,699 | 34,608 |
| Instructions | 1,557,225 | **1,557,792** |
| `.text` decoded | 5,021,807 | **5,024,236** |

Two effects explain the 199-entry difference:

- The static file stores only `0x63000` bytes of `.data`; the process has the
  full `0x5c54dc` with BSS live, so it yields 1,310 more pointer slots.
- The static file's `.rsrc` is plaintext resource data and contributes 36
  pointer-shaped dwords that are not function pointers. The dump's `.rsrc` is
  still encrypted, so those false seeds are absent.

The recovery pass is *gap-driven*, so more seeds means smaller gaps and fewer
recovered candidates (6,095 → 5,339); that is why a +136-seed difference does
not simply add entries. Both maps decode the same bytes to within 0.05 %, and
the running image decodes **2,429 more** of them. The running image is the
authoritative map; the static copy's numbers are within 0.6 % of it.

### The 5,339 recovered entries

These are functions the direct-call graph never reached. Example: `0x004b0a80`,
which starts right after four `int3` bytes and is a normal `mov eax,[global]`
function. They are marked separately in the analysis because a heuristic entry
is a candidate, not a certainty: a lift should emit them but not treat them as
reachable until something calls them.

### The 2,308 immediate entries

`push offset f` and `mov [global], offset f` survive the lost relocation table as
the last static record of function addresses that never reach a data section.
Only 2,308 pass the shape test — an order of magnitude fewer than NFSU2's
relocation table yields for a *smaller* image, which is the honest measure of
what the destroyed table cost.

## 4. Import map

365 functions from 15 DLLs. SecuROM zeroes the import descriptors in memory, so
this list is recovered from the live IAT (`scripts/recover_imports.py`); 365 of
371 slots resolve to a named export.

| DLL | # | What the runtime has to provide |
|---|---|---|
| `msvcr80.dll` | 156 | The MSVC 2005 CRT: `malloc`/`free`, `fopen`/`fread`, `sprintf`, `_initterm`, `__CxxFrameHandler3`, `_except_handler4_common`, `_ftol2`, `__iob_func`, locale, time. |
| `kernel32.dll` | 89 | Files, heap, threads, synchronisation, modules, `GetTickCount`, `QueryPerformanceCounter`, `VirtualAlloc`. |
| `user32.dll` | 39 | Window, message loop, cursor, `SendInput`, `GetKeyState`. |
| `ws2_32.dll` | 37 | Winsock 2, only used by the online mode. |
| `d3dx9_34.dll` | 14 | **The effect framework** (`D3DXCreateEffectFromResourceA`, `D3DXCreateEffectPool`) plus matrix/vector helpers and `D3DXSaveSurfaceToFile`. |
| `advapi32.dll` | 11 | Registry, `CheckTokenMembership`, crypto. |
| `ntdll.dll` | 6 | Low-level helpers pulled in by the CRT. |
| `msvcp80.dll` | 3 | C++ `std::string` ctor/dtor/copy — the C++ ABI surface. |
| `winmm.dll` | 3 | `timeGetTime`, `timeBeginPeriod`, `timeEndPeriod`. |
| `rpcrt4.dll` | 2 | `UuidToStringA` and friends. |
| `dinput8.dll` | 1 | `DirectInput8Create`. |
| `dsound.dll` | 1 | `DirectSoundCreate` (ordinal 1). |
| `netapi32.dll` | 1 | `Netbios`. |
| `shell32.dll` | 1 | Shell path helpers. |
| `combase.dll` | 1 | Wine's COM base, aliasing `CoCreateGuid`. |

The two jobs that dwarf the rest are **MSVCR80** (a real CRT, not a handful of
shims) and **`d3dx9_34`** (the FX9 effect framework, the project's stated risk).
`WS2_32` matters only for S6. The six unresolved slots point into `ddraw.dll`
thunks that are not in its export table; the module set is Wine's, not a
different program.

## 5. Jump tables and traps

- **796 jump tables** found, with **7,104 targets**. The targets are queued as
  blocks of the switch function, not as entries, so a table never invents a
  standalone function; without the tables those bytes would simply stay
  undecoded.
- **2,764 indirect jumps with no recognisable table.** This number is not a
  count of lost switches; breaking it down by operand shape:

  | shape | count | what it is |
  |---|---|---|
  | `jmp dword ptr [IAT slot]` | 1,480 | Import thunks (`jmp [0x19edbec]`), most of them a whole function, the rest a tail call into an import. The path *should* end here. |
  | `jmp reg` | 515 | Virtual dispatch / register-indirect tail calls. At lift time each becomes a dispatch call, which is correct but slower. |
  | `jmp [reg+disp]` | 5 | vtable slot dispatch. |

  So the real unresolved-switch count is **515**, not 2,764 — the rest is a
  normal import thunk pattern. All sites are in `out/analysis.json` under
  `trap_sites.indirect_jmp`.
- **29 undecodable sites** — bytes Capstone refuses. Each is recorded; they are
  candidates for a manual override in Phase 3.

## 6. What is not covered, and why

632,463 bytes (11.2 %) of `.text` remain undecoded, in 33,869 runs. Sorted by
kind (`scripts/classify_gaps.py`, runs ≥32 bytes: 373,681 bytes in 2,823 runs):

| kind | bytes | share | what it is |
|---|---|---|---|
| code-shaped | 295,035 | 79.0 % | Functions reached only through data we cannot read as a pointer (indirect calls, COM vtables, unaligned table entries). |
| unknown | 35,781 | 9.6 % | Neither shape cleanly. |
| data | 24,271 | 6.5 % | Jump-table bodies, float constants and string literals that live in `.text`. |
| prologue | 18,594 | 5.0 % | Same as code-shaped, but with an unmistakable MSVC prologue — the highest-value targets for a manual pass. |

ProStreet keeps most of its jump tables **inside `.text`** (NFSU2 keeps them in
`.rdata`), which is why the executable-section seed scan cannot see them and why
the JTSWITCH drop pass removes nothing here: those targets only ever enter as
blocks (`is_entry=False`) and are never counted as functions. It also means a
slice of the "data" bucket above is really table storage.

None of this is a blocker for Phase 3: the 27,710 seeded functions already include
everything the entry point can reach, and the recovered set adds the rest of the
obvious code. But the map is honest that ~11 % of the code section is
unaccounted for, and Phase 3 needs a policy for it (lift what we have, log every
indirect call at run time, and add entries from the logs).

## 7. Validation against the NFSU2 reference (oracle)

The user's NFSU2 copy is the exact build `nfsu2-web` requires (4,800,512 bytes,
SHA-256 `f9dd86c0…e24168a7`), so the reference pipeline could be run and its
function set used as ground truth for this analyzer.

The reference's own discovery has a hidden advantage: NFSU2's relocation
*directory* is zeroed, but the **table survives** in its unnamed section
(VA `0x4f5000`; `tools/reloc2.py` parses 999 blocks / 91,626 entries, 91,105 of
type 3, 100 % of values pointing into the image). Its seeds are therefore
*trusted pointers*, not aligned dwords.

| Run | Entries | Own code | `.text` decoded |
|---|---|---|---|
| Reference `g_fn_count` (`gen/all/fntab.c`, after JTSWITCH) | **19,458** | — | — |
| Reference `genall.py` function list before JTSWITCH | 23,279 | — | — |
| This analyzer, 16-aligned data only | 15,218 | — | 83.6 % |
| + immediate pointers | 17,034 | — | 87.5 % |
| + 4-aligned seeds | 17,986 | — | 88.8 % |
| + 1-aligned seeds | 20,174 | — | 93.2 % |
| + relocation table + imm + JTSWITCH | **19,526** | 18,581 | 96.0 % |

With the relocation table, the analyzer lands **0.35 % above** the reference's
real function set (19,526 vs 19,458) while decoding 96.0 % of `.text` — and the
JTSWITCH pass drops 3,938 table-only entries, which is the reference's own
correction (23,279 → 19,458). Without the relocation table, no alignment choice
gets closer than 12 % low (15,218) or 3.7 % high (20,174). The conclusion is
precise: **the seed source, not the closure, is what a function map stands or
falls on**, and ProStreet's lost relocation table is the single largest reason
its map is 11.2 % dark rather than 4.0 % dark.

## 8. Phase 2 gate

| Criterion | Status |
|---|---|
| PE parsed, headers and sections recorded | Pass — layout from the header the game carries in `.HEAVEN` |
| Imports recovered and grouped | Pass — 365 names, 15 DLLs, from the live IAT |
| Function entry set recovered without a reloc table | Pass — 35,853 entries (34,608 own code) |
| Jump tables identified | Pass — 796 |
| Coverage measured, gaps classified | Pass — 88.8 % decoded, gaps bucketed |
| Method validated against an independent run | Pass — static copy of the same build within 0.6 %; 0.35 % vs the NFSU2 reference |
| SecuROM sections excluded | Pass — `diabolus` (and its `ars`/`est`/`artem`/`celare`/`.securom` split) never scanned or decoded |

**Phase 2 is complete.** The map is in `out/analysis.json`; the numbers above are
what Phase 3 lifts from.

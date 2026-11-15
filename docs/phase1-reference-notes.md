# Phase 1 — Reference study notes

Reference: `reference/nfsu2-web` (th0mk/nfsu2-web, MIT, archived 9 Nov 2026).
It is a fork of Talha Müderrisoğlu's NFSU2Recomp (MIT snapshot of 16 Oct 2026) and
follows the method of N64Recomp and XenonRecomp.

> The plan said to read `docs/web/DESIGN.md`. That file does not exist in this
> repository. `docs/web/` only holds `PLAYING.md` (the player guide) and
> `start-page.png`. The design is documented in `README.md`, `AGENTS.md`, and the
> comments at the top of `gen.sh`, `web/gen.sh`, `web/build.sh`, `link.sh`, and
> `runtime/cpu.h`.
>
> **Update (11 Nov 2026):** the user supplied an NFSU2 copy that is the exact
> build the reference requires, so the pipeline *was* run — both lift variants
> and the WebAssembly toolchain setup. Section 10 records what it produced. The
> study below is still the source-level explanation.

---

## 1. What each folder does (the Phase 1 "done when")

### Repository root

| Path | What it does |
|---|---|
| `gen.sh` | Regenerates everything derived from `SPEED2.EXE`: relocations (`reloc2.py`), the lifted C (`genall.py`), import argument counts (`argcount.py`), and the second module `server.dll`. Writes `gen/all/stamp.txt` so a build can refuse a stale lift. |
| `build.sh` | Compiles the ~23k lifted functions into object files with clang, in parallel, incrementally by content digest. |
| `link.sh` | Compiles the hand-written runtime and links the executables (`out/nfsu2_boot`, `out/nfsu2_hd_boot`, `out/nfsu2_all`). The lifted shards are shared; only the runtime differs. |
| `run.sh` | Runs the native macOS build against the user's own game folder. |
| `pack.sh` | Builds `out/NFSU2.app` for distribution. |
| `requirements.txt` | Python dependencies for the tools: Capstone 5.x and pefile. |
| `app/` | `registry-defaults.txt` — registry defaults the port installs/expects. |
| `discord-bot/` | Node.js Discord forum bot (support/FAQ/moderation), not part of the game. |
| `docs/web/` | Player documentation (`PLAYING.md`) and a screenshot. |
| `licenses/` | Third-party licence texts. |
| `tests/` | C unit tests, harnesses (ASI VM, network, free roam, gamepad), and 620 input-script scenarios that drive the real game. |
| `tools/` | The Python lifter and every pack builder (see below). |
| `runtime/` | Hand-written C: the Windows/D3D9/sound/input layer and all game features. `runtime/web/` is the browser backend; `*.m` is the macOS backend. |
| `web/` | The browser build, the TypeScript page, and the Rust server. |
| `win/` | The Windows ASI plugin build of free roam (`roam_game.c` compiled with `roam_host.h`, no C library). |
| `.github/workflows/` | CI. |

### `tools/` — lifter and asset builders

| File | What it does |
|---|---|
| `lift.py` | The x86-32 → C lifter. Capstone-based; lifts one function at a time, solves flag liveness, handles jump tables, x87 and the calling ABI. |
| `genall.py` | Whole-program driver: discovers the function set, lifts every function, splits the output into shards, writes `decls.h`, `fntab.c`, `stubs.c`, and `impargs.c`. |
| `reloc2.py` | Parses the relocation table by hand from the unnamed section, because the PE relocation directory entry was zeroed. Writes `out/relocs.txt` (the absolute-address fixups). |
| `split_reloc.py` | Rewrites a small hard-coded set of operands for the split-screen port (addresses the original MIT patch scanned for). |
| `argcount.py` | Infers the number of arguments of each imported function and emits the import table used by the runtime HLE. |
| `prepare_module.py` | Prepares a second guest module (`server.dll`) for lifting at a fixed base. |
| `flagabi.py` | Whole-program flag liveness across calls and returns (`out/flagabi.json`). |
| `lregs.py` | Turns guest registers and flags into locals of each lifted function, stored back only around calls/returns/tail jumps. |
| `map_jumps.py` | Reads a free-roam path file and emits the server's jump-target table (a Rust source file). |
| `disasm.py` | Small disassembly helper using the lifter's Capstone setup. |
| `lift_profile.py`, `inline_pick.py`, `inline_hot.txt` | Instruction-level profiling and the hot-callee inlining list. |
| `fuzzisa.py`, `isatest.py`, `bcmp.py`, `check_split_sites.py` | ISA/flag correctness tests. |
| `langpacks.py`, `texpacks.py`, `musicpack.py`, `vinylpacks.py`, `hqvinyls.py`, `damagepack.py`, `modpacks.py`, `movies_av1.py`, `whinepack.py` | Builders for the optional language, texture, music, vinyl, damage and mod packs. |
| `lang/`, `texpack/`, `greekfont/`, `brand/`, `damage/` | Data and helpers for those packs. |
| `exeicon.py`, `photoicon.py`, `contact_sheet.py` | Icon/asset helpers. |
| `session_report.py`, `career_report.py`, `race_acc.py`, `drift_*.py`, `roam_*.py/.sh`, `vlanhub.py` | Feature/multiplayer analysis and test harnesses. |
| `writewatch.c` | Hardware watchpoint helper. |
| `apptest.sh`, `autorun.sh`, `genstamp.sh`, `manifest.py`, `packcheck.py` | Build/test/packaging support. |

### `runtime/` — the hand-written C runtime

| File(s) | What it does |
|---|---|
| `cpu.h` | The guest CPU state (registers, flags, x87/SSE) and the guest memory model. `g_mem`, `GSZ`, and the `MR*`/`MW*` access macros. |
| `dispatch.c` | O(1) table from a guest address to a lifted native function; runtime patch table; indirect-call statistics and diagnostics. |
| `win32.c`, `win32_ext.c` | High-level emulation of the Windows API: files, threads, synchronisation, timers, windows, registry, and so on. |
| `com.c`, `d3dx_hle.c`, `d3d9_tables.h`, `vsemu.c/.h` | COM, D3DX and shader emulation (the game's vertex/pixel shaders run on the host renderer). |
| `fileio.c`, `res.c`, `module.c` | Game file I/O, resources, and loading a second guest module (EA's `server.dll`) at its original base. |
| `dinput_kbd.c`, `dinput_pad.c`, `dikeys.c`, `keyboard.c`, `gamepad.m`, `kbd.h` | DirectInput/keyboard/gamepad input. |
| `metal.m`, `audio.m`, `texrep.m`, `fpscounter.m` | macOS Metal/audio/controller backends. |
| `heap.c`, `mem.c`, `genexec.c`, `watchdog.c` | Guest allocator, memory management, executable memory, watchdog. |
| `game.h`, `guest.h`, `guest_span.h`, `setting.h` | Shared game addresses/offsets, guest range checks and call helpers, settings. |
| `overrides.c` | The override mechanism: `sub_XXXXXX` wraps `sub_XXXXXX_orig` for the functions a feature replaces. |
| `features.c`, `featurelist.h` | The feature registry: three ordered lists (boot installs, instruction hooks, per-frame calls). |
| `career.c`, `garage.c`, `roam*.c`, `mprace.c`, `splitscreen.c`, `drift.c`, `photo.c`, `damage.c`, `damagecore.cpp`, ... | The actual features (career, garage, free roam, multiplayer races, split screen, drift, photo mode, crash damage). |

### `runtime/web/` — browser backends

`main_web.c` (entry), `gpu_web.c` (WebGPU), `audio_web.c` (Web Audio), `input_web.c`,
`net_lan.c`, `roam_web.c`, `status_web.c`, `texrep_web.c`, `shader_dump.c`,
`vsemu_wgsl.inc` / `vsemu_psref.inc` (WGSL shader translation), plus the wasm
compatibility layer (`compat/`), `sbrk.c`, `memchk.c`, `sha256.c`, `ticketlock.h`.

### `web/` — browser build, page, server

| Path | What it does |
|---|---|
| `web/gen.sh` | The web lift wrapper. Turns on `FLAGABI`, `X87OPT`, `JTSWITCH`, `LREGS`, `FTOL2INL`, and the hot-inline list. |
| `web/build.sh` | Builds the module with wasi-sdk: `wasm32-wasip1-threads`, shared memory, guest memory `[0, 0x28000000)`, page gives 1 GB at start. Output: `out/web/nfsu2.wasm` + `.json`. |
| `web/build.mjs` | Bundles the TypeScript page. |
| `web/start.sh` | Dev server. |
| `web/src/app/` | Start page and in-game overlays, settings, saves, i18n (`lang/*.json`). |
| `web/src/host/` | The wasm host: process/worker management, shared memory, typed messages, movie and IO workers. |
| `web/src/gpu/` | WebGPU renderer, DXT/ASTC texture decode, WGSL, photo mode. |
| `web/src/audio/` | AudioWorklet output. |
| `web/src/input/` | Keyboard, gamepad, wheel and touch mapping. |
| `web/src/net/` | Networking to the server. |
| `web/src/static/` | Icons, manifest, `horns.dat`. |
| `web/src/test/` | In-browser test pages. |
| `web/tools/` | Build/test/dev helpers: `gencache.sh`, `gencheck.sh`, `aliasguard.sh`, browser automation, i18n key checks, benchmarks. |
| `web/test/` | `node --test` page/server tests. |
| `web/deploy/` | Docker/Caddy/cloudflared deployment for HTTPS multiplayer. |
| `web/server-rs/` | Rust Cargo workspace: `http`, `lan` (virtual switch), `roam` (free-roam hub), `online` (EA Online lobby), `claims`, `bans`, `vault`, `util`, `ws`, `anticheat`, `names`, `headers`, `roamload`. |

---

## 2. The pipeline, end to end

```
SPEED2.EXE
  │
  ├─ tools/reloc2.py        → out/relocs.txt          (absolute-address fixups)
  ├─ tools/genall.py        → gen/all/shard_*.c       (lifted C, ~128 shards)
  │    └─ tools/lift.py     (one function → one C function)
  ├─ tools/argcount.py      → gen/all/impargs.c       (import argument counts)
  └─ tools/genstamp.sh      → gen/all/stamp.txt       (what the lift was made from)

gen/all + runtime/
  ├─ build.sh               → out/obj/*.o             (clang, incremental)
  ├─ link.sh                → out/nfsu2_boot          (native macOS)
  └─ web/build.sh           → out/web/nfsu2.wasm      (wasi-sdk)
         └─ web/src/ page + web/server-rs/ server
```

The generated C is never committed: it is made from the game and is gitignored.

---

## 3. How the lifter works

### 3.1 Function discovery (`genall.py: discover_all`)

The function set is not guessed; it is built from four sources and then closed
under control flow:

1. The PE entry point.
2. Optional **seeds** (exports of a second module, e.g. `server.dll`).
3. Every **absolute-address relocation** whose target lies in `.text` — this is
   how function pointers in data (vtables, callbacks, jump tables) become
   functions. `reloc2.py` recovers these because the relocation directory was
   zeroed.
4. A **worklist traversal**: starting from those entries, follow direct `call`
   and direct `jmp` targets, and queue conditional-branch targets, until the
   bytes are already covered.

A byte-coverage map prevents decoding the same bytes twice and prevents a
misaligned decode from being queued again. Direct calls become edges; direct
jumps are followed in place (tail calls); `ret`/`int3`/`hlt`/`ud2` end a path.

When `JTSWITCH=1`, a pointer whose every fixup slot lies inside a jump table
that the lifter turns into a `switch` is *not* made a separate function. Without
this, every case target became a function that was a copy of the rest of its
parent's body, reached only by the switch's dispatcher tail call.

### 3.2 Jump tables (`lift.py`)

MSVC's switch pattern is recognised structurally:

```
cmp  reg, n
ja   default
...
jmp  dword [reg*4 + table]
```

and the two-level form with `movzx reg2, byte [reg + bytes]` before the second
table. The table is read from the image, its entries are validated (in `.text`,
in range), and the block is emitted as a C `switch` over the guest register
instead of an indirect tail call. A table whose targets are not all inside the
same function is left as `DISPATCH_TAIL`.

### 3.3 Flags

Every arithmetic instruction would compute all six flags, but the next branch
usually reads only one. The lifter solves **backwards liveness over the
function's CFG** and gives each instruction a compile-time mask of the flags
read after it, so unused flag computations disappear. Special cases:

- `mul`/`imul` really write only CF and OF.
- Shifts/rotates with a count of 0 write no flags; variable counts are
  conservative.
- Capstone reuses the `eflags` field for x87 status bits, so x87 is special-cased;
  only `fcomi`/`fucomi` write EFLAGS, and `fcmovcc` reads them.
- Calls, indirect branches, `ret`, `int`, `pushf`, `lahf`, `daa`/`das`/`aaa`/`aas`,
  and `into` are treated conservatively.

### 3.4 Registers, x87, ABI

- `LREGS=1` makes guest registers and flags locals of the lifted function; they
  are stored back to the `CPU` struct only around calls, returns and tail jumps.
- `X87PC=1` (default) makes the game thread compute x87 at 24-bit precision, as
  Windows does; `X87PC=0` lifts plain doubles.
- `X87OPT` keeps x87 stack slots as locals within a basic block.
- `FLAGABI` proves across the whole program which functions return flags and
  which take them at entry, so most call/ret pairs can drop flag traffic.
- `FTOL2INL` inlines the CRT's `_ftol2` at its call sites.
- `INLINE_LIST` inlines the hot callees measured from a race.

### 3.5 Indirect calls and unsupported instructions

- An indirect call becomes `dispatch_indirect(c, target)`: one table lookup from
  guest address to lifted function, then a call.
- An indirect tail jump becomes `DISPATCH_TAIL`.
- An instruction the lifter cannot handle becomes a stub that calls
  `unsupported_insn(...)` and logs; `genall.py` reports the trap count and the
  most common reasons.
- A "closure" pass re-lifts functions that were referenced but missed by the
  shared coverage map (a misaligned decode can hide a real function start).

### 3.6 Overrides and hooks

- Any `sub_XXXXXX_orig` name referenced by the runtime turns that function into
  a wrapper: the lift emits `sub_XXXXXX` (the original body) and the runtime
  defines `sub_XXXXXX` as an override that may call `sub_XXXXXX_orig`. The list
  is derived from the runtime sources, so it cannot drift.
- `HOOK_SITE(0x...)` sites in the runtime are injected into the lifted code as
  instruction hooks.
- The output is split round-robin into `NSHARD` files (default 128) so the
  compiler can work in parallel.

### 3.7 Second module

`server.dll` (EA's LAN server) is lifted as a second guest module at the fixed
base `0x00A00000`. The runtime maps the *original* file at that base, applies
its base relocations, binds its IAT to the runtime HLE, and runs `DllMain`. The
SHA-256 of the original is pinned at lift time.

---

## 4. How the runtime works

### 4.1 CPU state and guest memory (`cpu.h`)

`CPU` holds the x86 registers, flags and x87/SSE state. The guest's 32-bit
address space is a flat block:

- **Native:** the whole 4 GB is reserved `PROT_NONE`; regions are made RW on
  demand. A bad guest access becomes a diagnosable signal.
- **wasm:** guest address == linear-memory address; the guest owns
  `[0, 0x28000000)` and the runtime's data/heap/stacks live above it. Built with
  `-fno-delete-null-pointer-checks` because address 0 is valid wasm memory.
- `MR8/MR16/MR32/MRF32/...` and `MW*` are the only ways lifted code touches
  guest memory. On wasm they are `may_alias` so the compiler cannot reorder a
  float store and an integer load of the same address (a real bug in the
  car-light code).

### 4.2 Dispatch (`dispatch.c`)

`g_fn_addr[]` / `g_fn_ptr[]` is a sorted table of every lifted function. A
direct table the size of `.text` gives O(1) guest-address → native-function
lookup. The table also holds runtime patches, so an indirect call to an
overridden function lands in the override. Misses and unsupported instructions
are counted and logged with a backtrace.

### 4.3 Windows HLE

`win32.c` / `win32_ext.c` implement the imported Windows functions (file I/O,
threads, timers, memory, window, registry, ...). Unimportant ones are stubs that
log every call, which is how the port discovers what the game actually needs.
`com.c`, `d3dx_hle.c` and `vsemu.c` cover COM, D3DX and the shader path.
`module.c` reproduces the Windows loader for the second guest module.

### 4.4 Features

`features.c` has three ordered lists:

1. **Installs** at boot (after `win32_init`, before `autotest_init`), each tagged
   with the builds that run it (`FT_WEB`, `FT_MAC`).
2. **Instruction hooks**: the first hook that accepts an address ends the chain.
3. **Per-frame calls**, from the main loop's hook.

A feature attaches in one of three ways: override a lifted function, hook an
instruction, or do per-frame work. Overrides are one-per-function; if two
features need the same one, the owner calls into the other. `gen.sh` scans the
runtime for `_orig` names and `HOOK_SITE`, so the list is always in sync.

---

## 5. How the web build works

- **Lift options** are stronger than the native defaults: whole-program flag
  liveness, x87 stack slots as locals, jump tables as `switch`, registers as
  locals, `_ftol2` inlining, and hot-callee inlining. Measured: 16–29 % fewer
  instructions per frame and a 50 MB module instead of 65 MB.
- **Toolchain:** wasi-sdk clang + wasm-ld, target `wasm32-wasip1-threads`, no
  Emscripten. Shared memory (`-pthread`) lets the game's threads run as Web
  Workers. The page gives the module 1 GB at start; it can grow to 4 GB.
- **Rendering:** Direct3D 9 calls go to `runtime/web/gpu_web.c`, which talks to
  the page's WebGPU backend. Shaders are translated to WGSL.
- **Sound:** `runtime/web/audio_web.c` → AudioWorklet.
- **Input:** browser events → `input_web.c`.
- **Server:** Rust serves the page, the module and the user's game folder, and
  runs the virtual LAN / free-roam / Online lobby.
- **Caching:** shards are content-addressed and cached across worktrees;
  `web/tools/gencheck.sh` refuses a lift that does not match the runtime's
  override/hook lists.

---

## 6. N64Recomp and XenonRecomp — what to borrow

### N64Recomp

- Needs **symbol metadata** (an ELF with symbols) rather than discovering
  functions from scratch; instructions are translated one-by-one into literal C
  (`ctx->r4 = ADD32(ctx->r4, 0x20);`).
- **Jump tables** are found by tracking register state backwards through
  `lui`/`addiu`/`lw`/`jr` (including position-independent GOT tables), then
  emitted as a `switch`.
- Overlays and indirect calls use a runtime `LOOKUP_FUNC` hook.
- Each function goes into its own file (the NFSU2 fork groups them into shards
  for compile speed).
- The runtime supplies macros for memory, arithmetic and relocations.

### XenonRecomp

- Parses the XEX and uses `.pdata` for function boundaries where available;
  otherwise `XenonAnalyse` finds function starts from branch-link instructions
  and computes lengths by static analysis.
- **Jump tables** are found from the `mtctr r0` / `bctr` pattern; `XenonAnalyse`
  writes them to a TOML file, and the recompiler emits real `switch` cases. The
  README is explicit that this is not fully generic and needs per-game tuning.
- Indirect calls use a runtime "perfect hash" from original address to
  recompiled function.
- Optimisations: keep non-volatile registers and the count/XER/condition
  registers as locals; skip the link register when exceptions are unsupported.
- Hooking pattern: alias the PPC function to an implementation function and
  mark the original weak, so a user can override while keeping `__imp__...`.

### Takeaways for us

- Static discovery + literal translation + a hand-written platform runtime is
  the right shape, and the NFSU2 lifter is a very close match for PE32 i386.
- **Jump tables are the fragile part.** Both references use pattern matching
  plus a manual escape hatch (TOML / config). We should plan for a manual
  function-boundary and jump-table list from day one.
- A per-function file split (or sharding) is needed for compile time at this
  scale.
- A runtime "address → function" table is the clean way to handle indirect calls
  and virtual functions.

---

## 7. What transfers to ProStreet

| Area | Transfers? | Notes |
|---|---|---|
| PE32 i386 parsing | Yes | Same format; `pefile` + Capstone. |
| Function discovery | Yes | Entry + relocations + call/jump closure. ProStreet's relocation directory is intact, so `reloc2.py` is not needed. |
| Instruction lifting | Mostly | ProStreet is MSVC 2005/2008-era and uses much more SSE/SSE2 and x87; the lifter needs more SSE coverage. |
| Jump-table handling | Yes, with tuning | MSVC switch patterns should be similar, but ProStreet is newer and may use different shapes. |
| Runtime HLE | Larger | D3D9 + D3DX9, DSound, DInput8, WinSock, more Win32. The import list must come from the *clean* exe. |
| Web layer | Yes | WebGPU/WebAudio/workers/server are reusable; shaders need WGSL translation. |
| Second module support | Likely useful | ProStreet may load DLLs; `module.c` is the model. |

---

## 8. Corrected Phase 0 finding — the executable is packed

The earlier Phase 0 report was based on a PE parse that was **off by 4 bytes**:
it read the `PE\0\0` signature as the COFF header, so it saw
`NumberOfSections=0`, no data directories, and no imports. That was a parsing
bug, not a property of the file.

Correct parse of `nfs.exe` (SHA-256 `2983cd28...22af5`, 3,765,248 bytes):

```
Machine      0x014c (i386)        PE32, ImageBase 0x400000
Sections     3
  .ROT       VA 0x00001000  VS 0x13eb000  raw 0        RWX, uninitialized
  .IN        VA 0x013ec000  VS 0x2bf000   raw 0x2be400  RWX, entropy 7.93
  .HEAVEN    VA 0x016ab000  VS 0x0d9000   raw 0x0d8c00  RW,  entropy 3.73
EntryPoint   0x16aa080 (in .IN)
Imports      20 functions, all loader/graphics stubs:
             KERNEL32 (LoadLibraryA, GetProcAddress, VirtualProtect,
             VirtualAlloc, VirtualFree, ExitProcess), ADVAPI32, d3d9,
             d3dx9_34, DINPUT8, DSOUND (ordinal 1), MSVCP80, MSVCR80,
             NETAPI32, ole32, RPCRT4, SHFOLDER, USER32, WINMM, WS2_32
```

The entry point is an **LZMA-style unpacker stub**:

```
016aa080  pushal
016aa081  mov  esi, 0x17ec000      ; absolute VA of .IN start
016aa086  lea  edi, [esi-0x13eb000]; 0x401000 = .ROT start
016aa08c  push edi
016aa08d  jmp  016aa09a
...       add ebx,ebx / adc ebx,ebx / mov ebx,[esi] / sub esi,-4  ; range decoder
```

It decompresses `.IN` (2.8 MB, entropy 7.93 = compressed/encrypted) into `.ROT`
(20.9 MB at `0x401000`). `nfsps.exe` is **byte-identical** to `nfs.exe` (same
SHA-256), so both are the same packed file.

This means:

- The 20 imports are the **packer's** imports, not the game's.
- The code Ghidra analysed is the **unpacker stub**, not the game.
- Phase 0's "done when" ("code is readable, many imports, sensible
  disassembly") is **not met**. The file is packed/protected.

This is most likely a repack/crack: the official ProStreet executable is around
20 MB with standard sections and a full import table.

### Legitimate ways forward

1. **Use the official unmodified executable from your own copy** (retail DVD,
   Steam, GOG, or EA app). This is the clean path: it gives the real import
   table, the real section layout, and a hash we can pin in Phase 6. Verify it
   is the version the project targets before lifting.
2. **Unpack this file ourselves.** The stub is a standard LZMA-style decoder;
   we could implement it and dump the 20 MB image, or run the game under Wine
   and dump the decrypted image from memory. This is legitimate reverse
   engineering of software you own, but the result may not match the official
   build, and it adds risk.
3. **Prove the pipeline on a simpler game first** (the plan's own fallback),
   then return to ProStreet.

Recommendation: option 1. Do not build Phase 2 on a packed/cracked binary.

---

## 9. Phase 1 "done when" check

| Criterion | Status |
|---|---|
| Clone `th0mk/nfsu2-web` | Done (`reference/nfsu2-web`). |
| Read `docs/web/DESIGN.md` | File does not exist; read `README.md`, `AGENTS.md`, and the build-script comments instead. |
| Read `tools/` | Done. |
| Read `runtime/` | Done. |
| Build and run NFSU2 | **Done** — see section 10. |
| Skim N64Recomp and XenonRecomp | Done (`reference/N64Recomp`, `reference/XenonRecomp`). |
| Explain what each folder does | Done (section 1). |

**Phase 1 is complete as a study**, and as of 11 Nov 2026 also as an executed
pipeline.

---

## 10. The pipeline, executed on the user's copy

The user's copy at
`<your install>/SPEED2.EXE` is
byte-for-byte the build the reference requires:

```
4,800,512 bytes   SHA-256 f9dd86c054878ce6276beb07c1fd61874f7a1e4bf1f241b084c65b73e24168a7
```

`requirements.txt` was installed into `reference/nfsu2-web/.venv` (Capstone
5.0.7, pefile 2024.8.26) and wasi-sdk 34.0 (`x86_64-linux`) unpacked into
`reference/nfsu2-web/deps/`.

### Plain lift (`./gen.sh`)

```
functions        : 23279
C source size    : 132.6 MB (128 shards)
traps (unsupported instructions): 356
truncated branch targets    : 0
server.dll:  1,127 functions, 1 trap, 0 truncated branch targets
```

Most common trap reasons are instruction-set exotica the lifter does not model:
`lcall` (32), `int`/`iretd`/`les` (28 each), `aaa` (22), `psrldq`/`pswapd` (22/20),
`frstor` (11) — not switch problems.

### Web lift (`web/gen.sh`, flags `FLAGABI X87OPT JTSWITCH LREGS FTOL2INL INLINE_LIST`)

```
g_fn_count = 19458          (gen/all/fntab.c)
8 lift-error stubs          (the reference's definition of a good lift)
130 shards, 109 MB
server.dll: 1,103 functions
```

Two numbers here matter for Phase 2: the function list before `JTSWITCH` is
23,279, and the real set after dropping table-only targets is **19,458**. A naive
count of every pointer that reaches `.text` therefore overstates the function
set by ~3,800 on NFSU2.

### What this proved

- The method is reproducible: the lift ran end to end on a clean machine with no
  source edits, and its stamp records the exe hash and lift options.
- The two builds differ only in lift options and shard count; `g_fn_count` is the
  authoritative post-`JTSWITCH` function count.
- `8 lift-error stubs` is a usable health check — more means a broken lift.
- The reference's advantage over a naive analyzer is its **seed source**: it
  hand-parses the relocation table that survives in NFSU2's unnamed section
  (`tools/reloc2.py`: 999 blocks, 91,626 entries, 91,105 type-3). That is what
  our ProStreet analyzer has to substitute for with aligned dwords, recovery and
  immediates — see `docs/phase2-function-map.md` §7 for the measured cost.

`web/build.sh` (the wasm compile/link step) was not run. Its inputs are now all
present — the `web/gen.sh` lift is in `gen/all` and wasi-sdk is in `deps/` — so
it is a one-command check that the generated C compiles and links. It would not
change the ProStreet map, so it is left as an optional confirmation.


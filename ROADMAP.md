# Roadmap

Each stage has a **gate**: a result you can measure, not a feeling. The order is
chosen so that the hardest unknown (the D3DX9 effect framework and shader
model 3) is de-risked before months are spent on the boring parts.

The reference project is the same scale as this one: `nfsu2-web` is ~154 k
lines of runtime/website/tools and 1,155 commits, built by several people over
more than a year. The executable is *not* the bottleneck here — it lifts
today. Plan for months, not weekends, and land it in the order below.

## S1 — Lift completeness and function-set audit (days)

* Fork the lifter into `tools/` (keep the MIT notice) so it can be changed.
* Implement the missing instructions: `lock xadd / cmpxchg / cmpxchg8b / add`,
  `pshufd`, `fstpnce`, far `call/jmp` + `iretd` (thunks), `les`, `das/aaa/aam`.
* Audit the 27,097-entry function set: every entry must start with a plausible
  prologue or be reached by a call. Use IDA/Ghidra's analysis as the oracle
  (count and address diff), and drop seeds that are really data.
* Never map the SecuROM sections; confirm no reachable code touches them.

**Gate:** a full lift whose `unsupported_insn` count in *reachable* code is 0,
and a function-set diff against Ghidra with no unexplained gaps.

**Status (2026-11-10):** lift complete — 27,097 functions, 98.1 MB C, 0 failures,
and **0 traps reachable from the entry point** (`scripts/classify_traps.py`).
Eight lifter gaps fixed; see `tools/README.md`. Remaining for the gate: the
Ghidra function-set diff.

## S2 — CPU bring-up, headless (weeks)

Reuse `runtime/web` from nfsu2-web (threads as workers on one shared memory,
`cpu.h`, `heap.c`, `fileio.c`, `win32.c`, `null_platform.c`).

* Map the image at `0x400000` (its preferred base; no relocations needed).
  `GUEST_END` must exceed `0x21f0000` (~34 MB) — `nfsu2-web` uses `0x28000000`,
  plenty.
* Import layer for the new names: **MSVCR80 (156)** and **MSVCP80 (3)** at the
  ABI level (`__CxxFrameHandler3`, `_CxxThrowException`, `__RTDynamicCast`,
  `_except_handler4_common`, `__iob_func`, `_initterm`, C++ `std::string`).
  Everything else (KERNEL32/USER32/ADVAPI32/ole32/SHFOLDER/WS2_32/NETAPI32/
  WINMM/RPCRT4) is a name-by-name port of the existing shims.
* File layer over the game folder; the game reads `GLOBAL`, `TRACKS`, … through
  the CRT and Win32 file APIs.
* Headless run: entry point → CRT init → threads → first `PeekMessage` /
  `Present` call. A frame counter in the null renderer is the progress metric.

**Gate:** the game boots headless for N frames without trapping, allocating
memory and loading data files the way it does on Windows (compare file-open
traces with a Windows run).

## S3 — Graphics: D3D9 + D3DX effects on WebGPU (the hard stage)

Two interleaved tracks.

**S3a — effect framework.** `d3dx9_34.dll` owns it here. Recommended first
move: lift the DLL as a second guest module (exactly how `nfsu2-web` lifts
`server.dll` and `modmgr` binds its imports) and run *its* code, then optimise
later. Generated C stays local; users supply the DLL from their own DirectX
install. Alternative: a hand-written HLE of the FX9 binary format.

**S3b — device and shaders.** Port `runtime/web/gpu_web.c` + `web/src/gpu` and
grow `vsemu`'s WGSL backend from ps_1_x/ps_2_0 to **ps_3_0/vs_3_0** (dynamic
branching, `texldl`, derivatives, 224+ pixel and 256+ vertex constants),
plus DXT textures, render targets, `StretchRect`, state blocks, D3DX surface
save (screenshots).

**Gate:** the intro and front-end render correctly and a race renders cars,
track and sky at a stable frame rate in Chrome. This gate is where the project
succeeds or needs a different renderer strategy.

## S4 — Audio and input (weeks)

* DirectSound → the existing `audio_worklet` path: 3D buffers, looping engine
  sounds, streaming music.
* DirectInput 8 → Gamepad API + keyboard (ProStreet's default scheme), with the
  existing action-mapping layer.
* `timeGetTime` / `timeBeginPeriod` pacing and the vsync lock.

**Gate:** menu music, engine and collision audio, and drivable input.

## S5 — The game (weeks to months)

Front-end, career map, race weekend (grip/drag/drift/speed), damage, saves in
`MEMCARD`. This is mostly a long tail of shims: every new menu reveals one more
API. Track it as one issue per broken screen.

**Gate:** career race weekend completes and saves/reloads.

## S6 — Online (months)

Winsock is already imported (40 functions). EA Nation is dead, so the port
needs its own rendezvous/relay server, like `nfsu2-web`'s Rust server, plus the
`ONLINE/` helpers (`bombd`, `lanserver`, `rebroadcasterlan`). Legal note: no EA
credentials, no pirated content, only player-hosted sessions.

**Gate:** two browsers race each other over the internet.

## Cross-cutting work

* **Performance:** wasm threads, guest lock granularity, effect-state memoising
  (the nfsu2-web D3DX memos are directly applicable), SIMD128 for the SSE2
  paths.
* **Determinism:** the game threads must not race; `nfsu2-web`'s ticket-lock
  model is proven.
* **Saves/settings:** browser storage, import/export of `MEMCARD` files.
* **Legal:** never commit generated C, game data or Microsoft DLLs; document the
  "bring your own copy" requirement.

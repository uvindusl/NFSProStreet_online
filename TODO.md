# Progress

Live checklist for the port. Stages and their gates are defined in
[ROADMAP.md](ROADMAP.md); measured facts are in
[docs/BINARY_REPORT.md](docs/BINARY_REPORT.md).

**Current stage: S1 close-out → S2 next.**
Last updated: 2026-11-10.

## S0 — Recon

- [x] Extract only `app/nfsps.exe` from the user's own repack (`game/`, gitignored)
- [x] Identify the image: v1.1, 28,739,656 bytes, SHA-256 `ee8a71c7…dce2f72`,
      ImageBase `0x400000`, **zero base relocations**, 371 imports / 14 DLLs
- [x] Confirm `.text` is decrypted and the entry point is stock
      `_WinMainCRTStartup` (`0x00828c25`)
- [x] Identify SecuROM sections (`ars`, `est`, `artem`, `celare`, `.securom`)
      and mark them never-mapped
- [x] Write [docs/BINARY_REPORT.md](docs/BINARY_REPORT.md) and
      [config/reference.json](config/reference.json)

## S1 — Lift

- [x] Fork the MIT lifter into `tools/` (`lift.py`, `genall.py`, `lregs.py`;
      `split_reloc.py` rewritten without NFSU2 hard-coded addresses;
      `retpast.txt` emptied)
- [x] Fix the 8 gaps found in real code: `lock`-prefixed
      `xadd`/`cmpxchg`/`add`, `cmpxchg8b`, memory-source `pshufd`, `fstpnce`,
      `repz ret`; document every diff in `tools/README.md`
- [x] Recover 13,342 function seeds from data pointers into `.text`
- [x] Full lift: **27,097 functions / 1,800,234 instructions / 98.1 MB C**,
      0 lift failures, 570 jump tables
- [x] Reachability audit (`scripts/classify_traps.py`): **0 traps reachable
      from the entry point**; 234 stubs remain only in dead/data functions
- [x] Verify every shard compiles with `clang -O1` after the final lift
      (`scripts/host_compile_check.sh`) — 38/38 in 334 s, one benign warning
- [x] Ghidra 12.2 headless analysis + address diff against the 27,097 entries;
      explained: lift-only entries are data seeds from pointer-into-.text
      discovery (26,719) and Ghidra-only entries are mostly Unwind functions
      (390 straddling instructions + 8 clean boundary). 0 traps reachable
      from the entry point confirmed.
- [x] `git init` and first source-only commit (nothing from `game/` or `work/`)

## Open-source readiness

- [x] Published at <https://github.com/uvindusl/NFSProStreet_online> (public,
      topics, CI badge, GitHub private vulnerability reporting enabled)
- [x] `AGENTS.md` (repo rules for humans and agents) + `tools/AGENTS.md`
- [x] `CONTRIBUTING.md`, `SECURITY.md`, `CODE_OF_CONDUCT.md`
- [x] `.editorconfig`, `.gitattributes`, MIT `LICENSE`, `THIRD_PARTY.md`
- [x] CI (`.github/workflows/ci.yml`): repo-clean guard + pyflakes + shell/JSON checks
- [x] Issue templates, PR template
- [x] `scripts/check_repo_clean.sh` (no game files, binaries or >2 MiB blobs)

## S2 — Headless CPU bring-up (next)

Plan: [docs/S2_BRINGUP.md](docs/S2_BRINGUP.md). Gate: boots headless for N
frames without trapping, with import and file-open traces comparable to a
Windows run.

- [x] Extract the full game data (~7.72 GiB `app/` tree) — `innoextract`, exit 0
- [x] Inventory the stability mods shipped with the reference install and map
      every Generic Fix option to what the port must do (docs/MODS.md,
      scripts/mod_inventory.py)
- [ ] Adapt nfsu2-web's `runtime/cpu.h`; map the image at `0x400000`
      (no relocations needed); reserve the guest arena above it
- [ ] TEB / `fs` setup: `fs:[0x18]` PEB, `fs:[0]` SEH chain (used by
      `_tmainCRTStartup` before any game code)
- [ ] Import dispatch: IAT slot = `0xF0000000 | index`; stdcall shims pop
      their arguments, cdecl shims do not; arity from `tools/argcount.py`
- [ ] MSVCR80 layer (156 imports: heap, `_initterm`, `_iob`, stdio, threads,
      locale, security cookie)
- [ ] MSVCP80 layer (3 C++/EH entry points: `__CxxFrameHandler3`,
      `_CxxThrowException`, `__RTDynamicCast`)
- [ ] KERNEL32 / USER32 / ADVAPI32 / ole32 / SHFOLDER / WINMM / RPCRT4 /
      NETAPI32 shims (name-by-name port of nfsu2-web's)
- [ ] Guest heap + `VirtualAlloc`/`VirtualQuery`; guest threads and
      synchronisation
- [ ] File layer over the extracted data: case-insensitive, one open-trace log
- [ ] Null renderer + frame counter + import-call histogram
- [ ] Gate: N frames headless, no trap; trace diff against Windows

## S3 — Graphics

- [ ] Lift `d3dx9_34.dll` as a second guest module (or HLE the FX9 format);
      bind its exports into the effect code
- [ ] D3D9 device: swap chain, state blocks, render targets, `StretchRect`,
      DXT textures, surface save
- [ ] Grow `vsemu`: ps_3_0 / vs_3_0 → WGSL (dynamic branching, `texldl`,
      `dsx`/`dsy`, 224+ constants)
- [ ] Gate: intro, front-end and a race render correctly in Chrome

## S4 — Audio and input

- [ ] DirectSound → WebAudio worklet: 3D buffers, looping engine, streaming
      music
- [ ] DirectInput 8 → Gamepad API + keyboard, action mapping
- [ ] `timeGetTime` / `timeBeginPeriod` pacing

## S5 — The game

- [ ] Front-end and career map
- [ ] Race weekend (grip / drag / drift / speed), damage
- [ ] Saves (`MEMCARD`)

## S6 — Online

- [ ] Replace the dead EA Nation service with our own server

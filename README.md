# Need for Speed: ProStreet — static recompilation

An experimental, from-scratch **static recompilation** of the 2007 PC game *Need for Speed: ProStreet*.

The game's x86 code is translated to C ahead of time. That C is compiled into a native binary (and, as a second target, to WebAssembly), and a hand-written Win32 / Direct3D 9 / D3DX high-level emulation layer stands in for Windows. The long-term goal is to run the game in a browser using WebGPU and Web Audio.

> **Status: early, native only.** The natively built game boots from a process dump of the executable, plays the intro movies, creates a profile, shows the main menu and plays a Race Day race (no sound, no window: frames are checked offline). The browser build does not run the game yet. See [Status](#status) for exactly what has been verified.

**Bring your own copy of the game.** This repository contains no game code, game data, shaders, or Microsoft DLLs. Everything derived from the game (lifted C, images, shader blobs) is generated locally and is git-ignored.

> Not affiliated with or endorsed by Electronic Arts. *Need for Speed* and *ProStreet* are trademarks of Electronic Arts Inc.

## Target executable

The lifter works on an **unpacked, unprotected** memory image of the ProStreet PC executable. The project verifies the image with `scripts/verify_target.py`; the expected fingerprint is:

| Property | Value |
|---|---|
| File size | **23,756,800 bytes** |
| `.text` SHA-256 | `963934d66f447eb21e81ca66f0df8d8e8d2a598acc9f0726c3dc1e68b5e65e31` |
| Machine | i386 (`0x014c`) |
| Image base | `0x00400000` |
| Entry point | `0x00828c25` (`_WinMainCRTStartup`) |
| Sections | `.text`, `.rdata`, `.data`, `.tls`, `.rsrc`, `diabolus` (6 sections) |
| `.text` size | 5,656,699 bytes (`0x56507b`) |
| Functions lifted | 55,175 (+ 5,399 diabolus functions) |
| Imports bound | 371 across 15 DLLs |

> The overall file SHA-256 is **not** checked — it differs between dump methods because the PE layout is rebuilt. Only the `.text` section hash is verified, ensuring the game code is identical.

The image must be placed at `artifacts/nfs.exe.unpacked` (or passed as an argument to the scripts). This file is git-ignored and never committed.

## Status

Each row is only marked ✅ when there is a command that reproduces it on the current build. Anything else is listed as not done.

| Area | State | How to check |
|---|---|---|
| x86 → C lift | ✅ 55,175 functions lifted | `scripts/lift_boot.sh artifacts/nfs.exe.unpacked` |
| Native runtime builds, warnings as errors | ✅ | `STRICT=1 scripts/build_runtime.sh` |
| Image loads, 371 imports bound | ✅ | `scripts/run_boot.sh` (see boot log) |
| C runtime start-up | ✅ completes (unpacked image post-load state) | `scripts/run_boot.sh` |
| Reaches `main` | ✅ `WinMain` running (0x004afee0) | `grep 'main-entry' out/boot.log` |
| D3D9 HLE: device, buffers, draw | ✅ native triangle test | `tests/build_d3d9_htriangle.sh` |
| D3D9 HLE: textures | ✅ native textured-quad test (API calls only, no pixel comparison) | `tests/build_d3d9_textured_quad.sh` |
| D3DX Effects & preshaders | ✅ all 34 effects parse; preshader interpreter matches | `tests/build_d3dx_test.sh`, `tests/build_runtime_lifecycle_test.sh` |
| Shader bytecode parsing | ✅ all 591 shaders parse | `lifter/tools/build_shader_disasm.sh` |
| D3D9 shader → WGSL translation | ✅ 591/591 emitted and pass `naga` validation (validity only, not correctness) | `scripts/validate_wgsl.sh` |
| Shader instruction tests | ✅ `texld`, address registers | `tests/d4_instruction_test.sh` |
| Web build | ✅ builds | `FULL=1 web/build.sh`, then `cd web && npm ci && node build.mjs` |
| WebGPU test triangle in Chrome | ✅ interpolated triangle verified by pixel sampling | `analysis/webgpu-test/` |
| A real ProStreet shader rendered correctly in WebGPU | ❌ not yet; output does not match expected colour | in progress |
| Game files: archive streaming | ✅ loads `GLOBALA.BUN`, `GlobalMemoryFile.bin`, parses `ATTRIBUTES.BIN`, loads car geometry/textures | `grep -E 'GLOBAL|CARS' out/boot.log` |
| Input, menus, a race (native) | ✅ scripted run: profile, main menu, Race Day event, into the race | `PROSTREET_SCRIPT_LOG=1 PROSTREET_FRAMEDUMP=999999 PROSTREET_SCRIPT=...` (script in [`docs/native-runs.md`](docs/native-runs.md)); the log shows `game flow 6` and `swrast: frame N: 986 draws` for the race frame |
| Offline software rasterizer | ✅ renders 3D frame render targets to `.ppm` files | `PROSTREET_FRAMEDUMP=300 scripts/run_boot.sh artifacts/nfs.exe.unpacked 15` |
| Audio | ❌ EA's AEMS sound modules are skipped | — |

The full milestone table is in [`docs/milestones.md`](docs/milestones.md). The roadmap to a playable game is in [`docs/ROADMAP.md`](docs/ROADMAP.md).

### Known issues

- **Native build runs headlessly.** The native build loads intro movies, title, profile creation and loading, main menu, and a Race Day event into the race. Frames are checked offline with `PROSTREET_FRAMEDUMP`, and scripted runs use `PROSTREET_SCRIPT`; see [`docs/native-runs.md`](docs/native-runs.md). A physical display window (SDL2/Vulkan) is the next milestone.
- **Retail game support.** The runtime auto-detects either `<gamedir>/nfsps.exe` or standard retail `<gamedir>/nfs.exe`. When running against packed retail executables lacking an uncompressed `.data` section, the runtime automatically zero-initializes the static ranges and applies the list head table.
- **Audio.** EA's AEMS audio modules are x86 modules loaded dynamically from sound data at runtime; calls into them are currently skipped.
- **"Valid WGSL" is not "correct WGSL".** `naga` confirms the output is well-formed. It does not confirm it computes the right result. An earlier version silently skipped unsupported opcodes (`dsx`/`dsy`); those are now emitted, but other opcodes may still be unhandled. Each shader family needs a rendered-output comparison against a reference.
- **Shader coverage is still per-opcode.** `cmp` and `lrp` match D3D9 in one synthetic CPU-interpreter case (`bash lifter/tools/cpu_shader_semantics_test.sh`); there is no corpus-wide or GPU differential for them, and other opcodes have not all been exercised numerically.
- **Performance and memory are unknown.** A 2007 game's code and assets inside a browser's 4 GB WebAssembly limit has not been tested.

## What you need

- A legitimately owned copy of the game. Point `game/` symlink (or `PROSTREET_GAMEDIR`) to your game installation directory.
- A **readable, unprotected build of the game executable.** The retail executable on some releases is wrapped in copy protection and cannot be lifted as shipped. This project does not provide, and does not document how to produce, an unprotected image. If you cannot obtain one legitimately, the lifter has nothing to work on.
- To build: Linux x86-64, Python 3, Node.js 20 or newer, a C compiler, and [wasi-sdk](https://github.com/WebAssembly/wasi-sdk/releases) unpacked into `deps/` for the web build. The tested setup is 8 cores and 15 GB of RAM; use `J=2` to limit parallel jobs.
- For shader validation: [`naga-cli`](https://crates.io/crates/naga-cli). `scripts/validate_wgsl.sh` runs it over every extracted blob.
- To view the web build: a Chromium-based browser with WebGPU enabled.

## Build and run

```bash
# 1. Lift the target executable
scripts/lift_boot.sh artifacts/nfs.exe.unpacked

# 2. Build the native runtime (STRICT=1 treats warnings as errors)
STRICT=1 scripts/build_runtime.sh

# 3. Boot natively (pointing game -> your installed game directory)
ln -s "/path/to/Need for Speed ProStreet" game
scripts/run_boot.sh artifacts/nfs.exe.unpacked 60

# 4. Or render and dump a specific frame offline (saved to out/frame_300_rt*.ppm):
PROSTREET_FRAMEDUMP=300 scripts/run_boot.sh artifacts/nfs.exe.unpacked 15
```

`scripts/build_runtime.sh` compiles the generated shards with `-w` and the hand-written runtime with `-Wall -Wextra`. `STRICT=1` turns any runtime warning into a build failure. Do not trust a build made without it.

Native tests:

```
tests/build_d3d9_htriangle.sh
tests/build_d3d9_textured_quad.sh
tests/d4_instruction_test.sh
```

Web build:

```
FULL=1 web/build.sh
cd web && npm ci && node build.mjs
```

## How it works

1. **`lifter/tools/`** — a Python lifter built on Capstone turns every reachable function of the executable into C. Switch tables, indirect calls, and function pointers held in data are seeded so the dispatcher can find them.
2. **`lifter/runtime/`** — the hand-written runtime: CPU state, a flat 4 GB guest address space, thread-local storage, structured exception handling, the Win32 layer, and Direct3D 9 / D3DX emulation.
3. **Shaders** — ProStreet ships 591 precompiled D3D9 shader blobs (`vs_1_1`, `vs_2_0`, `vs_3_0`, `ps_2_0`, `ps_3_0`). An offline translator turns them into WGSL for WebGPU. The blobs are read from your own game files and are never committed.
4. **`web/`** — the browser host: the WebGPU renderer, Web Audio, input, a worker-based game host, and a small server that serves the page, the module, and your local game folder.

More detail is in [`docs/`](docs/).

## Layout

| Path | Role |
|---|---|
| `lifter/tools/` | x86-32 → C lifter, shader disassembler, shader → WGSL translator |
| `lifter/runtime/` | native runtime: CPU, memory, dispatch, Win32 and D3D9/D3DX emulation |
| `web/` | browser host and web build |
| `scripts/` | lift, build, and boot drivers |
| `tests/` | native graphics and shader tests |
| `docs/` | design docs and milestone tracking — see [`docs/README.md`](docs/README.md) for an index |
| `analysis/` | working investigation notes and WebGPU test pages (may be outdated) |
| `debug/` | debug session logs and deep-dive notes (may be outdated) — see [`debug/README.md`](debug/README.md) for a topic index |
| `app/` | game configuration data (registry defaults) |

## Credits

- Based on the browser-port approach and x86 → C lifter of [th0mk/nfsu2-web](https://github.com/th0mk/nfsu2-web) (MIT), which in turn builds on Talha Müderrisoğlu's static recompilation of *Need for Speed: Underground 2*.
- Method follows [N64: Recompiled](https://github.com/N64Recomp/N64Recomp) and [XenonRecomp](https://github.com/hedge-dev/XenonRecomp).
- Parts of this project were developed with AI coding assistants.

Third-party attributions are listed in [`THIRD_PARTY.md`](THIRD_PARTY.md).

## License

MIT — see [`LICENSE`](LICENSE).

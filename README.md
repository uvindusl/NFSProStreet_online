# Need for Speed: ProStreet — in the browser

A fan project to run the 2007 PC game **Need for Speed: ProStreet** in a modern
browser, the way [nfsu2-web](https://github.com/th0mk/nfsu2-web) does for
Underground 2: translate the game's x86 code to C ahead of time, compile that C
to WebAssembly, and reimplement Windows/Direct3D 9/DirectSound on top of
WebGPU/WebAudio.

**Bring your own copy of the game.** No game code or game data is stored here:
the generated C, the compiled module and all game assets are produced locally
from a copy you legally own.

> Not affiliated with or endorsed by Electronic Arts. *Need for Speed* is a
> trademark of Electronic Arts Inc.

## Status

| Stage | What it means | State |
|---|---|---|
| **0. Recon** | know the executable: layout, imports, code coverage, DRM | ✅ done — see [docs/BINARY_REPORT.md](docs/BINARY_REPORT.md) |
| **1. Lift** | every function of `nfsps.exe` becomes C | ✅ 27,097 functions, 1.8 M instructions, 0 lift failures, **0 traps reachable from the entry point** |
| **2. CPU bring-up** | lifted code runs headless in wasm: CRT, heap, threads, file I/O | ⏳ next — plan in [docs/S2_BRINGUP.md](docs/S2_BRINGUP.md) |
| **3. Graphics** | D3D9 + the D3DX effect framework on WebGPU; shader model 2/3 | ⏳ |
| **4. Audio / input** | DirectSound → WebAudio, DirectInput 8 → keyboard/gamepad | ⏳ |
| **5. Game** | front-end, career, race weekend | ⏳ |
| **6. Online** | replace the dead EA Nation service with our own server | ⏳ |

Nothing in this repository runs the game yet. The lift is real, measured and
reproducible today; the runtime is the long part.

Live checklist: [TODO.md](TODO.md).

## What has been proven so far

Using the MIT-licensed lifter from `nfsu2-web` at commit
`9e02302` (see `ref-nfsu2-web/`, pinned by `scripts/lift.sh`):

* `nfsps.exe` (v1.1, 27.4 MiB) is a clean, statically analysable PE32 image
  once SecuROM's leftover sections are ignored. The code section is decrypted
  and contains ordinary MSVC 2005 x86; the entry point is stock
  `_WinMainCRTStartup`.
* 13,342 function entries were recovered from data pointers into `.text`
  (the image has **no base relocations**, so this replaces the reloc table).
* Discovery + lifting produced **27,097 functions / 1,800,234 instructions**
  into **98 MB of C** in **~100 s** on 8 cores, with **zero lift failures**.
* The lift has **no unsupported instruction left in code reachable from the
  entry point**. All 234 remaining stubs sit in function-shaped data
  (far-call bytes at pointer-table addresses); `scripts/classify_traps.py`
  proves the split by building the call graph from the generated C.
* Every shard compiles with `clang -O1` against the `nfsu2-web` runtime
  headers.
* The lifter fork carries eight fixes ProStreet needed (`lock`-prefixed
  atomics, `cmpxchg8b`, memory-source `pshufd`, `fstpnce`, `repz ret`) and
  drops Underground 2's hard-coded operand rewrites, which share ProStreet's
  `0x400000` base and could fire silently. See [tools/README.md](tools/README.md).

## Reproduce

```bash
# 1. tools
python3 -m venv .venv
.venv/bin/pip install -r ref-nfsu2-web/requirements.txt

# 2. your copy of the game (only the executable is needed for the lift)
cp "/path/to/NFS ProStreet/nfsps.exe" game/

# 3. recon + lift
.venv/bin/python scripts/analyze_exe.py game/nfsps.exe   # layout, imports, entropy
./scripts/lift.sh                                        # -> work/gen/shard_*.c
.venv/bin/python scripts/audit_lift.py work/gen/shard_*.c
.venv/bin/python scripts/classify_traps.py               # reachable vs dead traps
./scripts/host_compile_check.sh                          # clang on every shard
```

## Layout

```
game/          your nfsps.exe (+ extracted data); ignored by git
work/          generated C, seeds, logs; ignored by git
scripts/       recon, seeds, lift pipeline, audits, repo checks
tools/         forked x86->C lifter (upstream + ProStreet fixes)
docs/          binary report, S2 bring-up plan
config/        reference executable identity and lift options
ref-nfsu2-web/ pinned MIT checkout of the reference project (ignored by git)
AGENTS.md      rules for changing the repository (read this first)
TODO.md        live progress checklist
```

## Roadmap

See [ROADMAP.md](ROADMAP.md). The short version: the executable is not the
problem; the **D3DX9 effect framework** (which lives in `d3dx9_34.dll` for this
game, not in the exe) and the **shader model 3 translation to WGSL** are the
two hard problems, followed by the MSVC 2005 C++ runtime.

## Contributing

Issues and pull requests are welcome — start with [CONTRIBUTING.md](CONTRIBUTING.md).
Two rules above all others: **bring your own copy of the game** (no game files,
Microsoft DLLs or generated code in the repo, ever) and be decent to people.

* [TODO.md](TODO.md) — what is done and what is next
* [AGENTS.md](AGENTS.md) — conventions, pipeline, verification
* [SECURITY.md](SECURITY.md) — how to report a vulnerability privately
* [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md)

## Credits

* [nfsu2-web](https://github.com/th0mk/nfsu2-web) by th0mk (MIT) — the lifter,
  the wasm runtime, D3D9→WebGPU, audio and the browser host are its work.
* [NFSU2Recomp](https://github.com/TalhaMudo/NFSU2Recomp) by Talha Müderrisoğlu
  (MIT) — the original x86→C static recompiler for Underground 2.
* [pcrecomp](https://github.com/sp00nznet/pcrecomp) by sp00nznet — the generic
  PC recompilation toolbox; a reference for function-set recovery.
* [N64: Recompiled](https://github.com/N64Recomp/N64Recomp) and
  [XenonRecomp](https://github.com/hedge-dev/XenonRecomp) — the method.

## License

Our code: MIT (see [LICENSE](LICENSE)). Third-party notices:
[THIRD_PARTY.md](THIRD_PARTY.md).

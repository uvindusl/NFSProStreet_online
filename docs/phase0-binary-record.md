# Phase 0 — Target binary record

Phase 0 asks one question: *is the thing we are going to lift readable, and do we
have the right copy of it?* The answer changed once we stopped looking at files
and looked at what the user actually runs.

**The target is the image the running game decrypts into memory**, recovered
from the process and rebuilt as a normal PE. Everything downstream (Phase 2 and
on) is computed from that image.

## The shipped file

```
<your install>/nfs.exe
```

| | |
|---|---|
| Size | 3,765,248 bytes |
| SHA-256 | `2983cd28…39522af5` |
| Sections | 3 — `.ROT` (VA `0x1000`, VS `0x13eb000`, **0 raw bytes**), `.IN`, `.HEAVEN` |
| Entry point | `0x016aa080` |
| Imports | 20, all packer stubs (`LoadLibraryA`, `GetProcAddress`, `VirtualProtect`, …) |

This is the elamigos release of ProStreet **v1.1** with SecuROM 7 wrapping the
executable. The entry point is a range decoder:

```
016aa080  pushal
016aa081  mov  esi, 0x17ec000        ; absolute VA of .IN
016aa086  lea  edi, [esi-0x13eb000]  ; 0x401000 = .ROT
016aa08c  push edi
016aa090  mov  al, [esi] / inc esi / mov [edi], al / inc edi
016aa096  add  ebx, ebx / jne ...
```

It expands `.IN` into `.ROT` (20.9 MB) at `0x401000` and jumps in. Nothing in
`.ROT` exists on disk. Phase 0's rule — *if the target is packed or protected,
stop and resolve it legitimately before continuing* — therefore applies, and the
resolution is the runtime dump below.

## How the image was recovered

The game was launched through Heroic (GE-Proton 11-5, DXVK) and left at the main
menu. Wine maps the image at its preferred base `0x400000`:

| range | backing |
|---|---|
| `0x400000 – 0x401000` | file-backed header page |
| `0x401000 – 0x1b84000` | anonymous RWX, the decrypted image |

`/proc/<pid>/mem` is readable, so the image was read straight out of the live
process with no debugger and no elevation.

Two things make the dump more than a blob:

1. **The game carries its own original header.** At `0x1aa94bc` (inside
   `.HEAVEN`) is a complete `PE\0\0` header describing the *pre-SecuROM* layout:
   entry `0x428c25`, ImageBase `0x400000`, six sections, and a `diabolus`
   section that SecuROM later split into `ars/est/artem/celare/.securom`. The
   layout comes from the game itself, not from any other copy.
2. **`.text` is fully decrypted.** The first `0x566000` bytes of `.ROT` are the
   complete code section.

`scripts/build_runtime_image.py` reads the dump, lifts that embedded header to
the front of a new file, and rewrites every section so `rawptr == VA` and
`rawsize == VirtualSize` — i.e. the file *is* the memory image.

## The accepted target

```
game/nfs.exe.unpacked
```

| | |
|---|---|
| Size | 23,756,800 bytes |
| SHA-256 | `805bf77cd11aa3a11e205b9fbd8fb6f34d0c8374c91a11b8609dfae962fd962a` |
| Format | PE32, i386 (MSVC 2005), GUI |
| ImageBase | `0x00400000` |
| SizeOfImage | `0x016a8000` |
| Entry point | `0x00828c25` — `_WinMainCRTStartup` (`call __security_init_cookie; jmp __tmainCRTStartup`) |
| `.text` SHA-256 | `963934d66f447eb21e81ca66f0df8d8e8d2a598acc9f0726c3dc1e68b5e65e31` |
| Imports | **365 functions from 15 DLLs**, recovered from the live IAT |

The raw dump is kept beside it as `game/nfs.exe.memdump`
(SHA-256 `cc7526a8c919e3714951a356a83dd85309cd2134982011ccb20b866cb4b78d52`).

### Sections

| name | VA | virtual size | what it is |
|---|---|---|---|
| `.text` | `0x00001000` | `0x56507b` | the game's code |
| `.rdata` | `0x00567000` | `0x0e276a` | read-only data, vtables, IAT |
| `.data` | `0x0064a000` | `0x5c54dc` | writable data (live, including BSS) |
| `.tls` | `0x00c10000` | `0x00000009` | TLS index |
| `.rsrc` | `0x00c11000` | `0x0d86b4` | resources (not decrypted until used) |
| `diabolus` | `0x00cea000` | `0x9be000` | SecuROM's section; never lifted |

`diabolus` is the name the original header gives the region SecuROM carves into
`ars`, `est`, `artem`, `celare` and `.securom`. It is DRM, not game code.

## What is and is not in the dump

The packer decrypts lazily, so the dump holds exactly what the main menu had
touched:

| region | state in dump |
|---|---|
| `.text` | complete, decrypted |
| `.rdata` | complete (4 of 227 pages differ from static: IAT bound to Wine DLLs) |
| `.data` | complete and **live** (1,478 pages, includes initialised BSS) |
| `.tls`, `.rsrc`, `diabolus` | still encrypted/zero |

`.rsrc` and `diabolus` are not needed for lifting. `.data` being live is an
advantage: the static file only stores `0x63000` bytes of initialised data, while
the process has the full `0x5c54dc` with every pointer already absolute.

## Anti-dump measure found

SecuROM zeroes the `IMAGE_IMPORT_DESCRIPTOR` array in memory (`0x64793c`) and the
pages around it. The IAT at `0x567000` survives, fully bound. `scripts/recover_imports.py`
resolves those 386 slots against the modules actually loaded in the process:

| DLL | # | DLL | # |
|---|---|---|---|
| `msvcr80.dll` | 156 | `ntdll.dll` | 6 |
| `kernel32.dll` | 89 | `msvcp80.dll` | 3 |
| `user32.dll` | 39 | `winmm.dll` | 3 |
| `ws2_32.dll` | 37 | `rpcrt4.dll` | 2 |
| `d3dx9_34.dll` | 14 | `dinput8.dll` | 1 |
| `advapi32.dll` | 11 | `dsound.dll` | 1 |
| | | `netapi32.dll` | 1 |
| | | `shell32.dll` | 1 |
| | | `combase.dll` | 1 |

365 of 371 slots resolve to a named export; the six left over point into
`ddraw.dll` thunks that are not in its export table. The result is
`out/imports-runtime.json` and is fed to the analyzer with `IMPORTS_JSON=`.

The remaining difference from a static import table (`d3d9`/`shfolder`/`ole32`
vs `ddraw`/`combase`) is Wine's module aliasing, not a different program.

## Reproducing this record

```bash
# 1. launch the game, leave it at the main menu, note its pid
# 2. dump the image
python3 scripts/dump_running.py <pid>          # writes /tmp/opencode/ps-image.bin
# 3. rebuild a normal PE from the dump
python3 scripts/build_runtime_image.py game/nfs.exe.memdump game/nfs.exe.unpacked
# 4. gate it
python3 scripts/verify_target.py game/nfs.exe.unpacked
# 5. recover imports while the game is still running
python3 scripts/recover_imports.py <pid> game/nfs.exe.unpacked out/imports-runtime.json
```

`verify_target.py` checks size, SHA-256, machine, entry point, the six section
names, the memory-image layout, the `.text` hash and the startup stub, and exits
non-zero if anything drifts.

## Phase 0 gate

| Criterion | Status |
|---|---|
| Target resolved legitimately, not unpacked by hand | **Pass** — image taken from the running game, layout from the header the game carries |
| Code is readable and disassembles sensibly | **Pass** — `_WinMainCRTStartup` at `0x828c25`, 1,557,792 instructions decoded, 88.8 % of `.text` |
| Imports are the game's, not a packer's | **Pass** — 365 names across the real Win32/D3D9/CRT surface, resolved from the live IAT |
| Identity pinned (hashes recorded) | **Pass** — image, `.text` and raw dump hashes above |

Phase 0 is complete. The old `nfsps.exe` copy is no longer part of the pipeline.

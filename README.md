# Need for Speed: ProStreet — static recompilation

A from-scratch **static recompilation** of the 2007 PC game *Need for Speed:
ProStreet*. The game's x86 code is translated to C ahead of time, that C is
compiled into a native binary, and a hand-written Win32 / Direct3D 9
high-level emulation layer runs it.

This is the native sibling of the browser-port approach used by
[nfsu2-web](https://github.com/th0mk/nfsu2-web): the x86 → C lifter is reused
from that MIT-licensed project and retargeted from Underground 2 to ProStreet.

**Bring your own copy of the game.** No game code, game data or Microsoft DLLs
are stored here. The lifted C, the runtime image and all game assets are
produced locally from a copy you legally own.

> Not affiliated with or endorsed by Electronic Arts. *Need for Speed* is a
> trademark of Electronic Arts Inc.

## Status

| Phase | What it means | State |
|---|---|---|
| **0. Recon** | know the executable: layout, imports, DRM | ✅ [docs/phase0-binary-record.md](docs/phase0-binary-record.md) |
| **1. Reference** | study the NFSU2 lineage the lifter comes from | ✅ [docs/phase1-reference-notes.md](docs/phase1-reference-notes.md) |
| **2. Function map** | recover the real image and map every function | ✅ [docs/phase2-function-map.md](docs/phase2-function-map.md) |
| **3. Lift** | every needed function of `nfs.exe` becomes C | ✅ [docs/phase3-lifter.md](docs/phase3-lifter.md) |
| **4. CPU bring-up** | lifted code runs headless: CRT, heap, threads, TLS, SEH | ⏳ in progress |
| **5. Graphics** | D3D9 + the D3DX effect framework | ⏳ next |
| **6. Audio / input** | DirectSound, DirectInput | ⏳ |
| **7. Game** | front-end, career, race weekend | ⏳ |

The lift is real and measured; **nothing here runs the game yet.** The runtime
currently brings the CRT up, initialises per-thread TLS and dispatches SEH, and
stops inside `_initterm_e`.

## The target image

The retail `nfs.exe` is SecuROM-packed: its game code does not exist on disk,
only in memory after the loader decrypts it. The legitimate way to obtain it is
to run your own copy and read the decrypted image out of the process.

```bash
# 1. launch the game, leave it at the main menu, note its pid
# 2. dump the decrypted image
python3 scripts/dump_running.py <pid> artifacts/nfs.exe.memdump
# 3. rebuild a normal, loadable-layout PE from the dump
python3 scripts/build_runtime_image.py artifacts/nfs.exe.memdump artifacts/nfs.exe.unpacked
# 4. pin the image (size, hashes, sections, entry point)
python3 scripts/verify_target.py artifacts/nfs.exe.unpacked
```

`scripts/verify_target.py` exits non-zero if the image drifts from the recorded
one, so the rest of the pipeline fails loudly on the wrong copy.

## Build and run

```bash
scripts/lift_boot.sh                        # lift the boot set (JTSWITCH=1 required)
scripts/build_runtime.sh                    # -> out/prostreet
scripts/run_boot.sh artifacts/nfs.exe.unpacked 60
```

`scripts/build_runtime.sh` compiles the generated shards with `-w` and the
hand-written runtime with `-Wall -Wextra`; `STRICT=1` turns any runtime warning
into a build failure. Do not trust a build made with `-w`.

## Layout

| path | role |
|---|---|
| `lifter/tools/` | the x86-32 → C lifter (`lift.py`, `genall.py`, `lregs.py`, …) |
| `lifter/runtime/` | hand-written native runtime: CPU, guest memory, dispatch, Win32 HLE, SEH |
| `lifter/seeds-*.txt` | function seeds per phase |
| `scripts/` | image recovery, seeding, lift and build drivers |
| `docs/` | the phase records, written as each phase completed |

## License

MIT — see [LICENSE](LICENSE). Third-party attributions are in
[THIRD_PARTY.md](THIRD_PARTY.md).

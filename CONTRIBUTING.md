# Contributing

Thanks for helping. This project recompiles a commercial game to the browser;
please read the ground rules first.

## Ground rules

* **You must own the game.** Nothing derived from it — the executable, data
  files, extracted assets, generated C, compiled module, save games, or
  Microsoft DLLs — may be committed, attached to an issue, or uploaded
  anywhere. Describe the problem instead: "`sub_0050e2e0` traps at
  `0x0050e310` (`pshufd`)" is enough.
* **No piracy.** No links, keys, cracks, or instructions for obtaining the
  game or any Microsoft DLL.
* Be decent to people. See [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md).
* Contributions are accepted under the project license, MIT.

## What is most useful right now

Check [ROADMAP.md](ROADMAP.md) and the stage gates. The project is at **S1
done, S2 next** (headless CPU bring-up: MSVC 2005 runtime, Win32 import layer,
file layer). Anything that moves an S2 gate is worth more than a refactor.

Good first contributions:

* a Win32/CRT import shim with a test or a trace showing it works;
* a lifter fix for a trap reachable from the entry point
  (`scripts/classify_traps.py` lists them; currently none);
* documentation of a measured fact (see `docs/BINARY_REPORT.md`);
* build/CI improvements that do not require the game executable.

## Setup

```bash
python3 -m venv .venv
.venv/bin/pip install -r ref-nfsu2-web/requirements.txt
cp "/path/to/NFS ProStreet/nfsps.exe" game/
./scripts/lift.sh            # -> work/gen, a few minutes on 8 cores
```

`scripts/lift.sh` also clones/pins `ref-nfsu2-web` if it is missing. The
generated C stays in `work/` and is never committed.

## Before you open a pull request

1. Run `./scripts/check_repo_clean.sh` (no game files, no huge blobs).
2. If you touched the lifter: `./scripts/lift.sh` then
   `.venv/bin/python scripts/classify_traps.py work/gen game/nfsps.exe` and
   `./scripts/host_compile_check.sh`. Both must stay green.
3. If you touched runtime code: build it and say what you ran.
4. Say what you measured, before and after. For visual/audio work include a
   screenshot or a short clip. For lift work include the trap diff.
5. Keep commits focused and describe the *why*.

## Reporting a bug

Use the issue template. Include:

* what you did, what you expected, what happened;
* the guest address and instruction if the runtime trapped (a backtrace with
  `sub_xxxxxxxx` frames is ideal);
* your platform and browser / clang version;
* **never** attach the game or generated code.

## Code style

* Python 3.12+, `pefile` + `capstone` only. Scripts take paths as arguments
  and print what they measured.
* C11 for runtime code; the lifted C in `work/` is generated and never edited
  by hand.
* bash with `set -euo pipefail`.
* Match the surrounding code. No reformat-only commits.

## License

Our code is MIT (see [LICENSE](LICENSE)). Third-party components and their
licenses are listed in [THIRD_PARTY.md](THIRD_PARTY.md). By contributing you
agree your contribution is licensed under MIT.

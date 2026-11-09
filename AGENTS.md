# AGENTS.md

Instructions for anyone — human or AI — changing this repository.

## What this project is

A from-scratch port of **Need for Speed: ProStreet (2007)** to the browser,
built the way [nfsu2-web](https://github.com/th0mk/nfsu2-web) builds
Underground 2: statically translate the game's x86 to C with a recompiler
(`tools/`), compile that C to WebAssembly, and reimplement Windows,
Direct3D 9 / D3DX and DirectSound on WebGPU / WebAudio.

The executable is *not* the hard part and it already lifts. Read
[ROADMAP.md](ROADMAP.md) before planning work: the stages have gates and the
order matters (the D3DX effect framework and shader model 3 are the risk).

Current state: **S1 done** (full lift, zero traps reachable from the entry
point, C compiles). **S2 next** — see [docs/S2_BRINGUP.md](docs/S2_BRINGUP.md).

## Hard rules

These are not style preferences.

1. **Bring your own game.** No game executable, game data, Microsoft DLL,
   lifted C, save game or extracted asset may be committed, attached to an
   issue or uploaded anywhere. `game/` and `work/` are gitignored;
   `scripts/check_repo_clean.sh` and CI enforce the rule.
2. **No piracy assistance.** Do not add download links, keys, cracks or
   instructions for obtaining the game.
3. **Never map or execute the DRM sections** (`ars`, `est`, `artem`,
   `celare`, `.securom`). They are not part of the game; the `.text` image is
   already decrypted and is all we use.
4. **Do not invent measurements.** `docs/BINARY_REPORT.md` and
   `config/reference.json` contain measured facts about the reference
   executable. If a number changes, re-run the script that produces it and
   update both.

## Repository map

```
game/            your own nfsps.exe (+ extracted data); never in git
work/            seeds, generated C, logs, object files; never in git
ref-nfsu2-web/   pinned MIT checkout of the reference project; never in git
tools/           the forked lifter (see tools/AGENTS.md and tools/README.md)
scripts/         recon, seeds, lift pipeline, audits, checks
runtime/         (S2+) CPU state, memory, imports, Win32/D3D shims, wasm host
docs/            binary report, S2 bring-up notes
config/          reference executable identity + lift options
```

## Pipeline

```
nfsps.exe
   -> scripts/seed_scan.py      dwords in data sections that point into .text
   -> scripts/curate_seeds.py   diagnostic only: how many look like data
   -> tools/genall.py           discover callees + jump tables, lift to C
   -> work/gen/shard_*.c        (98 MB for 27,097 functions)
   -> scripts/audit_lift.py     count unsupported-instruction stubs
   -> scripts/classify_traps.py split them into "reachable from the entry
                                point" (bugs) and "dead" (data seeds)
   -> scripts/host_compile_check.sh   clang -O1 on every shard
```

`scripts/lift.sh` runs the whole lift. It takes a few minutes on 8 cores;
run it in the background rather than blocking a session.

## Commands

```bash
python3 -m venv .venv && .venv/bin/pip install -r ref-nfsu2-web/requirements.txt
cp /path/to/nfsps.exe game/
./scripts/lift.sh
.venv/bin/python scripts/audit_lift.py
.venv/bin/python scripts/classify_traps.py work/gen game/nfsps.exe
./scripts/host_compile_check.sh
./scripts/check_repo_clean.sh
```

## Conventions

* **Python**: 3.12+, stdlib plus the pinned `pefile`/`capstone`. Scripts are
  executable, start with `#!/usr/bin/env python3`, take paths as arguments
  (no hard-coded `/home/...`), and print what they measured.
* **C**: C11. The lifted output is compiler-generated, never hand-edited.
  Runtime code stays portable between wasm and a native host; anything
  compiler-specific needs a comment explaining why.
* **Shell**: bash, `set -euo pipefail`, paths derived from `$ROOT`.
* **Style**: match the file you touch (the lifter is dense Python; the
  scripts are plain and explicit). No reformat-only commits.

## Changing the lifter

Read `tools/AGENTS.md`. In short: minimal diffs, document every difference
from upstream in `tools/README.md`, prove the fix on a real function, then
re-lift and re-run both audits. A trap that is reachable from the entry point
is a bug; a trap in a data seed is noise.

## Changing the runtime (S2+)

* The import contract is in [docs/S2_BRINGUP.md](docs/S2_BRINGUP.md): IAT slot
  = `0xF0000000 | index`, stdcall shims pop their arguments, cdecl shims do
  not (the lifted call site already cleans up).
* Guest memory is untyped: the game stores floats with `fstp` and reads them
  back as integers. Never let the compiler assume otherwise (`may_alias` on
  the `MR*` types, as in nfsu2-web's `cpu.h`).
* One guest thread = one `CPU` struct. Guest threads are real host threads on
  one shared guest memory.
* Every shim that is still a stub must log its name and arguments. "Runs
  further" is measured by the import histogram and the file-open trace, not
  by a feeling.

## Pull requests

State what you measured, before and after. For rendering or audio changes,
include a screenshot or a short recording; for lift changes, include the
`classify_traps.py` diff. Keep `README.md` and `ROADMAP.md` honest — they are
the project's interface.

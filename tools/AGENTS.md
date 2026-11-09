# tools/ — rules for the forked lifter

These files are a fork of nfsu2-web's lifter (MIT). Read
[tools/README.md](README.md) for what changed and why. This file is about how
to change it further.

## Provenance

* Upstream: `https://github.com/th0mk/nfsu2-web`, commit
  `9e0230212776f6c9a4d0db30c1963e9730138553`, files `tools/lift.py`,
  `tools/genall.py`, `tools/lregs.py`, `tools/retpast.txt`.
* `split_reloc.py` is a rewrite, not a copy.
* Keep the MIT notice. Do not copy files from the reference checkout that are
  not needed by the lifter.

## Rules

1. **Minimal diffs.** Upstream fixes are easy to merge only while the diff
   stays small. Do not restructure the lifter for taste, do not rename
   upstream symbols, do not reformat files you are not fixing.
2. **Every behavioural difference is documented** in the table in
   `tools/README.md` and in the docstring at the top of `lift.py`. A fix
   without a documented symptom is not accepted.
3. **No Underground 2 addresses.** `split_reloc.py` and `retpast.txt` were
   emptied because upstream's hard-coded addresses belong to a different
   binary that happens to share the `0x400000` base. Never paste them back.
   If ProStreet needs an address-keyed rewrite, add it with a comment naming
   the function and the reason.
4. **A trap is only a bug if it is reachable.** `scripts/classify_traps.py`
   builds the call graph from the generated C and separates traps reachable
   from the entry point from those in data seeds. Add a lifter feature only
   for the former; `lcall`/`les`/`das` in data are noise.
5. **Do not filter seeds by guesswork.** Lifting a data seed produces an
   unreachable function; dropping a real seed also drops everything reachable
   only through it. See the header of `scripts/curate_seeds.py`.

## Validation loop for a lifter change

```bash
# 1. lift the one function you are fixing, with trapping off
.venv/bin/python - <<'EOF'
import importlib.util
spec = importlib.util.spec_from_file_location('L', 'tools/lift.py')
L = importlib.util.module_from_spec(spec); spec.loader.exec_module(L)
lf = L.Lifter('game/nfsps.exe', comments=False, trap=False)
code, _ = lf.lift(0x90c7f0)          # <- the function
print(len(code), 'bytes of C')
EOF

# 2. full pipeline (background, ~3 min)
./scripts/lift.sh

# 3. gates: zero reachable traps, then every shard compiles
.venv/bin/python scripts/classify_traps.py work/gen game/nfsps.exe
./scripts/host_compile_check.sh
```

The generated C is derived from the game: never commit `work/`, never paste a
shard into an issue. Quote the function address and the trapped instruction
instead.

# AGENTS.md

Guidance for AI coding agents and new contributors working in this repository.

## Project

Static recompilation of *Need for Speed: ProStreet* (2007 PC). The x86 executable is lifted to C, compiled natively, and supported by a hand-written Win32/D3D9/D3DX HLE runtime. Browser/WebGPU is a second target. **The game is not yet runnable.**

## Non-negotiables

- **No copyrighted assets**. Never commit or upload game binaries, game data, extracted shaders/blobs, Microsoft DLLs, or `artifacts/nfs.exe.unpacked`. Generated outputs under `out/` are local artifacts.
- **Evidence before status claims**. A milestone is only done when a command/log reproduces it. See `docs/milestones.md`.
- **Runtime warnings are build failures** under `STRICT=1`. Keep `lifter/runtime/` clean.
- **Minimal, targeted changes**. Prefer small fixes with verification over broad rewrites.

## Where to look

| Need | Path |
|---|---|
| Project status | `README.md`, `docs/milestones.md` |
| Roadmap/blockers | `docs/ROADMAP.md`, `analysis/prostreet-blockers.md` |
| Lifter | `lifter/tools/`, `docs/phase3-lifter.md` |
| Runtime/HLE | `lifter/runtime/`, `docs/import-coverage.md`, `docs/d3dx-coverage.md` |
| Web port | `web/`, `docs/web-build-and-hle-port-guide.md` |
| Debug history | `debug/README.md`, `debug/*.md` |
| Tests/scripts | `tests/`, `scripts/` |

## Common commands

```bash
scripts/verify_target.py artifacts/nfs.exe.unpacked   # verify local image
scripts/lift_boot.sh                                  # JTSWITCH=1 required
STRICT=1 scripts/build_runtime.sh                     # build native runtime
scripts/run_boot.sh artifacts/nfs.exe.unpacked 300    # boot + log
scripts/validate_wgsl.sh                              # WGSL validation (naga)
```

## Agent files

- `.github/agents/*.agent.md` - GitHub Copilot / VS Code custom agents.
- `.github/copilot-instructions.md` - repository Copilot instructions.
- `.agents/` - framework-neutral rules/workflows/skills/memory (`.agent` symlinks here).
- `.claude/CLAUDE.md` and `.config/opencode/...` may contain local environment instructions; follow them when present, but repo safety rules above always apply.

## Making changes

- For runtime boot issues, capture `tail -120 out/boot.log` and identify the first fault/blocker before patching.
- For lifted-code issues, prefer fixing lifter/runtime generation inputs; generated `out/lift-boot/shard_*.c` is not a normal edit target.
- For docs, include proof commands and avoid speculative percentages.
- For PRs, describe the blocker, the fix, verification commands, and any remaining risk.

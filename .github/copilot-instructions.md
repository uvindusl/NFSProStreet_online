# Copilot Instructions - NFS ProStreet Static Recompile

This repository is an experimental static recompilation of *Need for Speed: ProStreet* (2007 PC). It contains no copyrighted game assets.

## Start here

- `README.md` - status, build commands, known issues.
- `docs/milestones.md` - evidence-based milestone checklist.
- `docs/ROADMAP.md` - phased roadmap.
- `docs/README.md` - doc index.
- `AGENTS.md` - agent/contributor operating rules.

## Build & verify

```bash
scripts/lift_boot.sh                        # lift boot set (JTSWITCH=1 required)
STRICT=1 scripts/build_runtime.sh           # native runtime; warnings are errors
scripts/run_boot.sh artifacts/nfs.exe.unpacked 300
```

Web:

```bash
FULL=1 web/build.sh
cd web && npm ci && node build.mjs
```

## Repository rules

- **No copyrighted assets**: never commit `artifacts/nfs.exe.unpacked`, game files, extracted shaders/blobs, or generated `out/`.
- **Evidence over claims**: do not mark milestones done without a reproducing command/log.
- **Runtime warnings are failures** with `STRICT=1`; keep hand-written runtime clean.
- **Minimal fixes**: prefer targeted HLE/runtime/lifter fixes; avoid broad speculative refactors.
- **Generated code**: `out/lift-boot/shard_*.c` is generated; fix the lifter/runtime as appropriate, not by hand-editing generated shards unless explicitly doing a throwaway debug.

## Key areas

- `lifter/tools/` - x86->C lifter and shader tools.
- `lifter/runtime/` - CPU/memory/dispatch/Win32/D3D9/D3DX HLE.
- `web/` - browser host and WebGPU/Web Audio path.
- `scripts/` - lift/build/run/validation drivers.
- `tests/` - native tests.
- `debug/` - investigation notes.

## Custom agents

Repository agents are defined in `.github/agents/*.agent.md`. Use the most specific agent for the task (runtime debug, lifter/shader, web port, docs/triage).

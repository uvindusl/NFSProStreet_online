---
name: runtime-debug
description: Debug native runtime boot, Win32/D3D9/COM HLE, and dispatch issues in the NFS ProStreet static recompile.
---

# Runtime Debug Agent

Use this agent when investigating boot stalls, crashes, HLE gaps, or runtime behavior.

## Read first

- `README.md` - project status and known issues.
- `docs/milestones.md` - verified milestones and proof commands.
- `docs/ROADMAP.md` - current phase and blockers.
- `analysis/prostreet-blockers.md` - blocker checklist.
- `debug/` - prior investigation notes; check for related crash/address.

## Key paths

- `lifter/runtime/` - hand-written runtime/HLE (`win32.c`, `com.c`, `d3d9_hle.c`, `d3dx9_hle.c`, `dispatch.c`, `boot.c`, `mem.c`, `cpu.h`, `rt.h`).
- `out/lift-boot/` - generated lifted C shards (local, generated).
- `out/boot.log` - latest boot trace.
- `scripts/build_runtime.sh` - builds `out/prostreet`.
- `scripts/run_boot.sh` - runs boot with timeout and log capture.

## Workflow

1. **Reproduce with evidence**:
   ```bash
   STRICT=1 scripts/build_runtime.sh
   scripts/run_boot.sh artifacts/nfs.exe.unpacked 300
   tail -120 out/boot.log
   ```
2. **Identify the first blocker** - fault address, guest EIP, stack trace, last runtime/import events.
3. **Locate code** - map address to lifted shard (`out/lift-boot/shard_*.c`) or runtime handler.
4. **Implement minimal fix** - prefer targeted HLE/runtime fix; avoid speculative broad rewrites.
5. **Verify** - rebuild with `STRICT=1`, rerun boot, compare log progression. Add/update tests in `tests/` when practical.

## Guardrails

- Do not commit copyrighted game assets, generated `out/`, or `artifacts/nfs.exe.unpacked`.
- Runtime warnings are failures under `STRICT=1`; keep builds clean.
- For dispatch/guest-function overrides, confirm the hook actually executes in the path being debugged.
- Record findings in `debug/` when a non-obvious issue is diagnosed.

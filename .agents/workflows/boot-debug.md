# Boot Debug Workflow

Use for native boot crashes, stalls, or HLE gaps.

## 1. Reproduce

```bash
STRICT=1 scripts/build_runtime.sh
scripts/run_boot.sh artifacts/nfs.exe.unpacked 300
tail -120 out/boot.log
```

Capture: fault address/EIP, guest stack, last imports/runtime events, and log size/progression.

## 2. Locate

- Map guest address to lifted shard: `rg -n "sub_XXXXXXXX" out/lift-boot/shard_*.c` or search address.
- Check runtime handlers in `lifter/runtime/` (`win32.c`, `com.c`, `dispatch.c`, `d3d9_hle.c`, `d3dx9_hle.c`, `boot.c`, `mem.c`).
- Check prior notes in `debug/` and `analysis/prostreet-blockers.md`.

## 3. Fix

- Patch the first evidenced blocker.
- Prefer targeted HLE behavior or narrow dispatch guard that is actually executed.
- If adding a dispatch override, ensure the symbol/path is registered and reached.

## 4. Verify

```bash
STRICT=1 scripts/build_runtime.sh
scripts/run_boot.sh artifacts/nfs.exe.unpacked 300
tail -120 out/boot.log
```

Compare before/after: same crash site, progressed further, or new blocker. Record new blocker with evidence.

## 5. Report

Include command, old/new log signature, files changed, and remaining risk. Do not claim fixed without log evidence.

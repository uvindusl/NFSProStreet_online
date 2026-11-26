# Runtime HLE Skill

## When to use

Boot/runtime work involving Win32, COM, D3D9/D3DX, threading, file I/O, or dispatch.

## Inputs

- `out/boot.log` tail and crash signature.
- Guest address/EIP/stack trace.
- Relevant runtime source in `lifter/runtime/`.

## Procedure

1. Reproduce with `STRICT=1 scripts/build_runtime.sh && scripts/run_boot.sh artifacts/nfs.exe.unpacked 300`.
2. Find first blocker, not downstream symptoms.
3. Inspect existing HLE/import coverage in `docs/import-coverage.md` and `docs/d3dx-coverage.md`.
4. Implement smallest compatible behavior; log sparingly and usefully.
5. Verify boot progression/regression with same command.

## Output checklist

- Files changed.
- Before/after log evidence.
- Any behavior assumptions/incompatibilities.
- Tests added or reason not practical.

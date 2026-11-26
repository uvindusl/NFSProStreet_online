# Lifter Skill

## When to use

Issues in x86->C lifting, function recovery, indirect calls/switches, ABI, or generated runtime integration.

## Inputs

- Target address/function/symbol.
- Lifted shard output in `out/lift-boot/`.
- Lifter sources in `lifter/tools/`.

## Procedure

1. Reproduce with `scripts/lift_boot.sh` or targeted lifter command.
2. Confirm the root cause in lifter logic, not by editing generated C.
3. Make deterministic, narrow changes.
4. Rebuild runtime with `STRICT=1 scripts/build_runtime.sh`.
5. Validate with boot/test command relevant to the affected path.

## Output checklist

- Root cause in lifter/tooling.
- Regenerated artifact expectation.
- Verification command/result.
- Any remaining unsupported pattern.

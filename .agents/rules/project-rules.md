# Project Rules

## Safety / Legal

- Never commit or upload copyrighted game assets: `artifacts/nfs.exe.unpacked`, game archives/data, extracted shaders/blobs, Microsoft DLLs, or proprietary binaries.
- `out/` and generated artifacts are local build outputs; do not commit unless explicitly intended and safe.
- Do not include secrets, tokens, or personal credentials in files or commits.

## Engineering

- Build runtime with `STRICT=1 scripts/build_runtime.sh` when validating changes; runtime warnings are treated as failures.
- Prefer minimal, evidence-backed fixes. Identify the first failing address/blocker before patching.
- Generated lifted C in `out/lift-boot/` is not a normal source-edit target; fix lifter/runtime inputs instead.
- Keep native and web HLE behavior aligned when touching shared semantics.

## Evidence

- Status claims must be reproducible with commands/logs.
- Distinguish observed, inferred, and unverified.
- Add debug notes under `debug/` for non-obvious investigations.

## Git / PRs

- Use clear commit messages and small logical commits.
- PR description should include: problem/blocker, root cause (if known), fix, verification commands/results, and remaining risks.
- Do not push unrelated local worktree changes.

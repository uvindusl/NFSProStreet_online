# Project Memory (Non-sensitive)

Last updated: 2026-11-26

## Facts

- Target: static recompile of NFS ProStreet PC; no copyrighted game assets in repo.
- Native boot/build commands: `STRICT=1 scripts/build_runtime.sh`, `scripts/run_boot.sh artifacts/nfs.exe.unpacked 300`.
- Generated lifted code lives in `out/lift-boot/`; `out/` is local generated output.
- Runtime HLE lives in `lifter/runtime/`; lifter/shader tools in `lifter/tools/`; web target in `web/`.

## Current known state (verify before relying)

- README reports boot reaches WinMain and asset loading/parsing, but attribute tree validation/message loop/D3D9 lifecycle remain blockers.
- PR review workflow exists at `.github/workflows/opencode-pr-review.yml` (OpenCode OIDC, posts PR comment).
- PR #11 boot smoke test on 2026-11-26 still hit `SIGSEGV guest_va=0x00000022` at `sub_00530f60` eip `00531064`; dispatch-indirect guard did not execute in lifted function path.

## Do not store here

- Secrets, tokens, credentials.
- Copyrighted game data, extracted binaries/shaders, or proprietary content.
- Unverified claims presented as fact.

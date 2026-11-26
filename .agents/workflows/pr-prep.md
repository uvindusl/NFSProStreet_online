# PR Prep Workflow

Use before opening or updating a pull request.

## 1. Scope

- Confirm branch has only related changes: `git status --short --branch`.
- Do not include generated `out/`, `artifacts/`, game files, or unrelated worktree edits.

## 2. Verify

Run the smallest relevant proof:

```bash
STRICT=1 scripts/build_runtime.sh
scripts/run_boot.sh artifacts/nfs.exe.unpacked 300   # if runtime touched
scripts/validate_wgsl.sh                              # if shader/WGSL touched
cd web && npm ci && node build.mjs                    # if web touched
```

Record exact commands and key output.

## 3. Document

PR description should include:

- **Problem/blocker** - observed behavior, log/address.
- **Root cause** - confirmed or suspected; label uncertainty.
- **Fix** - what changed and why.
- **Verification** - commands run and results.
- **Risks/follow-ups** - what is still unverified.

## 4. Safety check

- No secrets/tokens.
- No copyrighted assets or extracted blobs.
- No speculative status claims in docs.

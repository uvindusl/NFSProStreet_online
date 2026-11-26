# Release / PR Checklist

- [ ] Change is scoped; no unrelated files.
- [ ] No secrets/tokens added.
- [ ] No copyrighted game assets/generated blobs committed (`artifacts/`, `out/`, extracted shaders, game files).
- [ ] Relevant build/tests run and recorded.
- [ ] `STRICT=1 scripts/build_runtime.sh` passes if runtime/shared C changed.
- [ ] Boot log evidence captured if boot behavior changed.
- [ ] Docs updated where status/behavior changed; claims are evidence-based.
- [ ] PR description includes blocker, fix, verification, risks.

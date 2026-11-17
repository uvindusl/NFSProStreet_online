# ProStreet WGSL Validator — Real Validation with naga

## Validator used: naga-cli v30.0.2

**No real WebGPU validator was available in this environment:**

- **Chrome/Chromium headless WebGPU:** `navigator.gpu` exists but `requestAdapter()` returns no adapter (headless, no GPU). Even with `--enable-features=WebGPU,SwiftShader --use-gl=swiffsher`, no adapter is found. `createShaderModule` + `getCompilationInfo` cannot be exercised.
- **glslangValidator:** Does not support WGSL input (only GLSL/HLSL/SPIR-V stages like `.vert`, `.frag`). Not usable.
- **naga-cli v30.0.2:** Installed via `cargo install naga-cli`. Used as the real WGSL validator.

naga validates WGSL by parsing and type-checking; no output files means validation-only mode.

## Validation command

```
naga <shader.wgsl>
```

Exit code 0 + "Validation successful" on stdout = pass. Non-zero exit or parse/type error = fail.

## WGSL source

All 591 shaders translated from D3D9 bytecode to WGSL using `out/tools/shader2wgsl` (built by `lifter/tools/build_shader2wgsl.sh`):

```
out/tools/shader2wgsl <shader.bin> -o <shader.wgsl>
```

Blobs at `/tmp/opencode/ps-shaders/*.bin`, WGSL at `/tmp/opencode/ps-wgsl/*.wgsl`.

## Validation Results

```
bash /tmp/opencode/validate-wgsl.sh
=== emitted=591 emitfail=0 naga_fail=0 ===
```

**All 591 shaders pass naga validation.** Exit code 0, no parse or type errors. The oDepth fix (depth.x output) is confirmed working across the full corpus.

## Per-Profile Breakdown

| Profile | Total | Passed | Failed |
|---------|-------|--------|--------|
| ps_2_0 | 223 | 223 | 0 |
| ps_3_0 | 71 | 71 | 0 |
| vs_1_1 | 27 | 27 | 0 |
| vs_2_0 | 199 | 199 | 0 |
| vs_3_0 | 71 | 71 | 0 |
| **Total** | **591** | **591** | **0** |

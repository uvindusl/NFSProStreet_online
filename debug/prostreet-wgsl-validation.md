# ProStreet WGSL shader validation — all 591 translate

## Method

Ran `out/tools/shader2wgsl` (built by `lifter/tools/build_shader2wgsl.sh`, links `web/runtime/web/runtime/vsemu.c` + `web/runtime/web/vsemu_wgsl.inc`) on every `.bin` in `/tmp/opencode/ps-shaders` (the 591 extracted D3D9 shader blobs from `.rsrc` of the clean retail image; see `debug/prostreet-shader-capture.md`).

Usage:
```
out/tools/shader2wgsl <shader.bin> [-o out.wgsl]
```
A shader **passes** if `shader2wgsl` exits 0 and emits WGSL. It **fails** on non-zero exit (e.g. `vs_compile` failure, WGSL emission failure `rc <= 0`, unsupported operand type, buffer overflow).

The WGSL emitter (`vsemu_wgsl.inc`) handles:
- **oDepth** (`R_DEPTHOUT`): emits `PSOut { @location(0) col, @builtin(frag_depth) depth }` entry point
- **texld** (`O_TEXLD`/`O_TEX`, opcode 66): `textureSample(texN, smpN, uv)`
- **texkill** (`O_TEXKILL`, opcode 65): `if (any(reg < 0)) { discard; }`

## Validation command (reproduce)

```bash
for blob in /tmp/opencode/ps-shaders/*.bin; do
    out/tools/shader2wgsl "$blob" >/dev/null 2>&1 || echo "FAIL: $blob"
done
```

Or the built-in test driver: `lifter/tools/shader2wgsl_test.sh`.

## Results

| Profile | Total | Passed | Failed | oDepth | texld | texkill | Other |
|---------|-------|--------|--------|--------|-------|---------|-------|
| ps_2_0 | 223 | 223 | 0 | 0 | 118 | 38 | 0 |
| ps_3_0 | 71 | 71 | 0 | 0 | 71 | 0 | 0 |
| vs_1_1 | 27 | 27 | 0 | 0 | 0 | 0 | 0 |
| vs_2_0 | 199 | 199 | 0 | 0 | 0 | 0 | 0 |
| vs_3_0 | 71 | 71 | 0 | 0 | 0 | 0 | 0 |
| **Total** | **591** | **591** | **0** | **0** | **189** | **38** | **0** |

- **Passed: 591 / 591 (100%). Failed: 0.**
- Failure types: oDepth = 0, texld = 0, texkill = 0, other = 0.
- Every shader using texld (189) and every shader using texkill (38) translates successfully.
- **No shader uses oDepth** (`R_DEPTHOUT` register type = 9): 0 shaders, so the `PSOut`/`frag_depth` path is exercised only by the emitter's struct template, not by any real shader.

## Reconciliation with earlier "58 oDepth failures" claim

An earlier analysis reported "533 of 591 translate; 58 failures, all ps_2_0, all oDepth." That was produced against a **stale** `shader2wgsl`/`vsemu_wgsl.inc` that predated oDepth/texld/texkill support. The current emitter (built 10:08) handles oDepth via the `PSOut` struct, texld via `textureSample`, and texkill via `discard`, so **all 591 shaders now translate**. The earlier oDepth/texld/texkill failure categories are now all zero.

## Files

- `/tmp/opencode/ps-shaders/*.bin` — 591 extracted D3D9 shader blobs (not in repo)
- `out/tools/shader2wgsl` — the translator binary
- `lifter/tools/shader2wgsl_test.sh` — built-in test driver
- `debug/prostreet-texture-gap.md` — R_ADDR / texld / texkill usage histogram
- `debug/prostreet-shader-corpus.md` — corpus table, sizes, reproduction commands

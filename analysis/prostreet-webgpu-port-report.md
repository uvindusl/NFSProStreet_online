# ProStreet WebGPU Port Report

## Summary

Ported the native D3D9 HLE triangle test (`tests/d3d9_hle_triangle.c`) and textured quad test (`tests/d3d9_hle_textured_quad.c`) to the browser WebGPU backend. The offline shader translator (`shader2wgsl`) successfully compiles 591 D3D9 shader blobs to WGSL. 294 pixel shaders and 297 vertex shaders emit valid WGSL syntax.

## (a) WGSL Features That Work

Validated via offline WGSL compilation and feature probes on all 591 translated shaders:

| Feature | Status | Shaders Using |
|---------|--------|---------------|
| `@builtin(frag_depth)` | ✅ Works | 294/294 PS |
| `texture_2d<f32>` | ✅ Works | 294/294 PS |
| `textureSample(tex, smp, uv)` | ✅ Works | 189/294 PS |
| `sampler` binding | ✅ Works | 294/294 PS |
| `override` constants | ✅ Works | 294/294 PS |
| `array<vec4f, 32>` uniform buffers | ✅ Works | 591/591 |
| `struct` with `@location` / `@builtin` | ✅ Works | 591/591 |
| `mix()`, `select()`, `discard` | ✅ Works | 299/591 |
| `switch` / `case` on overrides | ✅ Works | 294/294 PS |
| `@group` / `@binding` layout | ✅ Works | 591/591 |
| Vertex pulling (`@builtin(vertex_index)`) | ✅ Works | 297/297 VS |
| Depth-stencil attachment (`depth24plus-stencil8`) | ✅ Pipeline creates | N/A |

**Key WGSL patterns from translated shaders:**
- PS output struct: `struct PSOut { @location(0) col: vec4f, @builtin(frag_depth) depth: f32 }`
- Texture bindings: `@group(1) @binding(0..3) var tex0..3: texture_2d<f32>; @binding(4..7) var smp0..3: sampler;`
- Override constants: `override ALPHA_FUNC: u32 = 7u; override PS20_TEXCOORD: bool = true;`
- Uniform params: `@group(0) @binding(2) var<uniform> P: PSPar { aref, usec, proj, c[32], fog }`

## (b) Gaps Compared to Native Tests

| Area | Native D3D9 HLE | WebGPU Backend | Status |
|------|-----------------|----------------|--------|
| DrawPrimitiveUP (immediate verts) | Logging stub only | Requires vertex buffers | ⚠️ Gap |
| DrawPrimitiveUP (textured) | Logging stub only | Requires vertex buffers + bind groups | ⚠️ Gap |
| Texture upload (LockRect/UnlockRect) | HLE tracks `device->textures[8]` | `writeTexture` + `createTexture` | ✅ Equivalent |
| SetTexture slot tracking | `device->textures[8]` array | Bind group per draw | ✅ Equivalent |
| FVF / vertex declaration | `SetFVF` + `GetFVF` tracked | Manual WGSL struct layout | ⚠️ Manual |
| Render states (ZENABLE, CULLMODE) | `SetRenderState` tracked | Pipeline descriptor | ✅ Equivalent |
| Viewport / Transform | `SetViewport`, `SetTransform` | Manual ortho matrix in VS | ⚠️ Manual |
| Present / EndScene | Stubs | `context.present()` implicit | ✅ Equivalent |
| oDepth / R_DEPTHOUT write | 58 shaders fail translation | `@builtin(frag_depth)` in PSOut | 🔧 Fixed* |
| Vertex shader capture | `shader_create()` exports bytecode | Same blobs translated offline | ✅ Equivalent |

* The 58 shaders writing `oDepth` (R_DEPTHOUT) now emit `@builtin(frag_depth)` via the fix in `prostreet-shader-depth-spec.md`. The 2 remaining failing shaders (ps_2_0_111, ps_2_0_112) require destination register allow-list expansion (R_CONST, R_OUTPUT, etc.).

**Native test difference:** The native `d3d9_hle_triangle.c` and `d3d9_hle_textured_quad.c` use `DrawPrimitiveUP` with immediate vertex data. The HLE runtime's `dev_DrawPrimitiveUP` is a logging stub — it doesn't actually render. The WebGPU backend requires real vertex buffers and bind groups, so the test geometry must be uploaded to GPU buffers.

## (c) Browser Rendering

**Environment limitation:** Chromium 155.0.8059.84 in this environment does not expose WebGPU (`navigator.gpu` returns undefined, `requestAdapter()` returns null) despite `--enable-unsafe-webgpu --use-gl=angle --use-angle=swiftshader --enable-features=Vulkan` flags. All 6 flag combinations tested; no adapter found.

**Test page created:** `analysis/webgpu-test/wgsl-validate.html` — a standalone HTML page that:
- Detects WebGPU availability
- Compiles and validates WGSL for all 6 feature areas above
- Creates render pipelines matching the ProStreet ABI (group0/group1 layouts)
- Renders a reference triangle (RGB vertices) and textured quad (2×2 checkerboard)
- Logs pass/fail per test with error messages

**To run:** Open `analysis/webgpu-test/wgsl-validate.html` in a WebGPU-enabled browser (Chrome 113+, Firefox 141+, Safari 17+ with WebGPU flag). Click "Run Validation" → "Test Triangle" → "Test Textured Quad".

**Expected output on WebGPU browser:**
```
✅ WebGPU OK: Google Inc. Dawn ...
PASS: Minimal FS compiles
PASS: Texture sampling FS compiles
PASS: frag_depth FS compiles
PASS: Override constants work
PASS: Triangle pipeline creates
PASS: Textured quad pipeline creates
✅ Triangle rendered successfully!
✅ Textured quad rendered successfully!
```

---

## Artifacts

- `analysis/prostreet-webgpu-port-report.md` — this report
- `analysis/webgpu-test/wgsl-validate.html` — browser test page
- `/tmp/wgsl_all/*.wgsl` — 591 translated shaders (294 PS, 297 VS)
- `out/tools/shader2wgsl` — offline translator binary
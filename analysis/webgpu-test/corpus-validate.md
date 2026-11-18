# ProStreet draw_real — WebGPU/Tint acceptance validation (8-wide ABI)

Goal: prove the emitted WGSL is accepted by a REAL WebGPU implementation (Tint
via Chromium), not just naga, and render a real ProStreet pixel shader behind
the CURRENT 8-wide inter-stage ABI.

> REFRESHED for the 8-wide ABI (coordinator, verified). The page now inlines the
> **current** emitter output: `VOut` has 11 locations (`col0, col1, uv0..uv7,
> fog` with `fog` at `@location(10)`) and group 1 is `tex0..tex7` at bindings
> 0..7 plus `smp0..smp7` at bindings 8..15. The previous revision of this file
> quoted the 4-wide module and reported `ok:false` for a *half-screen* triangle
> that the committed page did not use; the page and this doc now agree.

Files (all under `analysis/webgpu-test/`):
- `draw_real.html` — inlines the verbatim WGSL emitted for `ps_2_0_042` (entry
  point renamed `ps_0` → `ps_042`), builds the pipeline with the REAL explicit
  ABI layouts from `web/src/gpu/shader-abi.ts`, supplies K/P/H uniform blocks +
  a 1x1 white texture, draws a full-screen triangle into an offscreen texture,
  reads pixels back, and prints `DRAW_REAL`.
- `draw_real_capture.js` — Playwright + Chromium capture script.
- `draw_real.png` — screenshot of the rendered page.

## Step 1 — Emit WGSL for `ps_2_0_042.bin`

`ps_2_0_042` is 3 instructions (`def c0`, `mov r0,c0`, `mov oC0,r0`) and emits
solid `(1,1,1,0.5)`.

```
bash lifter/tools/build_shader2wgsl.sh
out/tools/shader2wgsl /tmp/opencode/ps-shaders/ps_2_0_042.bin -o /tmp/opencode/ps042.wgsl
```

**Emitted WGSL** (verbatim; only the entry point renamed `ps_0` → `ps_042`):

```wgsl
// ps_0: ps_2_0, 3 instructions
struct VOut {
  @builtin(position) pos: vec4f,
  @location(0) col0: vec4f, @location(1) col1: vec4f,
  @location(2) uv0: vec4f, @location(3) uv1: vec4f, @location(4) uv2: vec4f, @location(5) uv3: vec4f,
  @location(6) uv4: vec4f, @location(7) uv5: vec4f, @location(8) uv6: vec4f, @location(9) uv7: vec4f,
  @location(10) fog: f32,
}
struct PSPar { aref: f32, usec: f32, pad0: f32, pad1: f32, proj: vec4f, c: array<vec4f, 32>, fog: vec4f }
@group(0) @binding(2) var<uniform> P: PSPar;
override ALPHA_FUNC: u32 = 7u;
override PS20_TEXCOORD: bool = true;
fn alpha_pass(a: f32, r: f32) -> bool {
  switch ALPHA_FUNC {
    case 1u: { return false; }
    case 2u: { return a < r; }
    case 3u: { return a == r; }
    case 4u: { return a <= r; }
    case 5u: { return a > r; }
    case 6u: { return a != r; }
    case 7u: { return !(a < r); }
    default: { return true; }
  }
}
fn ps_end(c: vec4f, fog: f32) -> vec4f {
  if (ALPHA_FUNC != 8u) { if (!alpha_pass(c.a, P.aref)) { discard; } }
  return vec4f(mix(P.fog.rgb, c.rgb, mix(1.0, saturate(fog), P.fog.a)), c.a);
}
fn tcoord(q: vec4f, sel: f32) -> vec2f {
  let m = i32(sel) >> 4u;
  return q.xy / select(select(1.0, q.z, m == 1), q.w, m == 2);
}
struct PSOut { @location(0) col: vec4f, @builtin(frag_depth) depth: f32 };
@group(1) @binding(0) var tex0: texture_2d<f32>;
@group(1) @binding(1) var tex1: texture_2d<f32>;
@group(1) @binding(2) var tex2: texture_2d<f32>;
@group(1) @binding(3) var tex3: texture_2d<f32>;
@group(1) @binding(4) var tex4: texture_2d<f32>;
@group(1) @binding(5) var tex5: texture_2d<f32>;
@group(1) @binding(6) var tex6: texture_2d<f32>;
@group(1) @binding(7) var tex7: texture_2d<f32>;
@group(1) @binding(8) var smp0: sampler;
@group(1) @binding(9) var smp1: sampler;
@group(1) @binding(10) var smp2: sampler;
@group(1) @binding(11) var smp3: sampler;
@group(1) @binding(12) var smp4: sampler;
@group(1) @binding(13) var smp5: sampler;
@group(1) @binding(14) var smp6: sampler;
@group(1) @binding(15) var smp7: sampler;
@fragment fn ps_0(i: VOut) -> @location(0) vec4f {
  var r0 = vec4f(0.0);
  let c0 = vec4f(1.0, 1.0, 1.0, 0.5);
  var oC = vec4f(0.0);
  r0 = c0;
  oC = r0;
  return ps_end(oC, i.fog);
}
```

## Step 2 — Pipeline with the REAL explicit ABI layouts

- **group0**: binding 0 read-only-storage (VB), binding 1 uniform K
  (`hasDynamicOffset`, `minBindingSize: 65536`), binding 2 uniform P
  (`hasDynamicOffset`, `minBindingSize: 560`), binding 3 uniform H
  (`hasDynamicOffset`, `minBindingSize: 656`).
- **group1**: bindings 0–7 `texture_2d<f32>` (`sampleType: 'float'`,
  `viewDimension: '2d'`), bindings 8–15 `sampler` (`type: 'filtering'`).
- **pipeline layout**: `[group0, group1]` — explicit, NOT `layout: 'auto'`.
- `setBindGroup(0, bg0, [0, 0, 0])` — three dynamic offsets for K, P, H.

Buffers: VB (empty storage), K window (65536 zero bytes), P (560 zero bytes,
`aref=0`), H (656 zero bytes), a 1x1 white texture, one filtering sampler bound
to all 8 sampler slots.

**Vertex shader:** hand-written full-screen triangle `(-1,-1),(3,-1),(-1,3)`
(placeholder, NOT the emitted VS — noted in the page). Its `VOut` matches the
pixel shader's 11-location input; `uv4..uv7` are zero vectors.

## Step 3 — Render and readback

Render to an offscreen texture matching `navigator.gpu.getPreferredCanvasFormat()`,
copy to a `MAP_READ` buffer, map and read the centre pixel (128,128) and a 4x4
patch at the origin (top-left).

## Step 4 — Captured run

```
timeout 200 node analysis/webgpu-test/draw_real_capture.js   # exit 0
```

**DRAW_REAL line (exact):**

```
DRAW_REAL {"ok":true,"center":[255,255,255,128],"patch":[[[255,255,255,128],[255,255,255,128],[255,255,255,128],[255,255,255,128]],[[255,255,255,128],[255,255,255,128],[255,255,255,128],[255,255,255,128]],[[255,255,255,128],[255,255,255,128],[255,255,255,128],[255,255,255,128]],[[255,255,255,128],[255,255,255,128],[255,255,255,128],[255,255,255,128]]],"expected":[255,255,255,128],"note":"ps_2_0_042 emits c0=(1,1,1,0.5); P.aref=0 so alpha_pass(0.5,0)=true; P.fog.a=0 so no fog blend"}
```

The full-screen triangle covers the entire render target (it spans NDC
`x∈[-1,3]`, `y∈[-1,3]`), so every read pixel is the shader's output. All 17
probed pixels are exactly `[255,255,255,128]` = `c0=(1,1,1,0.5)` after
`ps_end` with `P.aref=0` (alpha test passes) and `P.fog.a=0` (no fog blend).

## Conclusion

- **WGSL accepted by Tint (real WebGPU):** YES — `createShaderModule` succeeded,
  no WGSL parse/validation errors.
- **Pipeline created with the real 8-wide ABI:** YES — explicit layout with
  dynamic offsets for K/P/H and 8 textures + 8 samplers in group 1.
- **Real ProStreet pixel shader rendered:** YES — `DRAW_REAL ok:true`.
- **PNG:** `analysis/webgpu-test/draw_real.png`.

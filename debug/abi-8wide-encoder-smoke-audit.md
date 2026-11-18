# Encoder 8-wide group-1 smoke test audit

Audits `analysis/webgpu-test/encoder-smoke.html` (commit 3fffbfc) against the real
sources. The page *replicates* the layout rather than importing it, so each field is
checked by hand. **Verdict: the page faithfully replicates the non-cube (`abi.group1`)
layout; no divergence.**

| # | Feature | Real source | Verdict | Detail |
|---|---------|-------------|---------|--------|
| 1 | group1 textures: bindings 0..7, `sampleType: 'float'`, `viewDimension: '2d'` | `web/src/gpu/shader-abi.ts:60-62` | **MATCH** | Real source: `texture: { sampleType: 'float', viewDimension: cube && k === 0 ? 'cube' : '2d' }`, and `group1 = ... textures(false)` (`:66`), so for the non-cube group1 **every** binding 0..7 is `'2d'`. Page: `{ binding: k, visibility: F, texture: { sampleType: 'float', viewDimension: '2d' } }` for all 8 — identical. (Binding 0 is `'cube'` only in the separate `group1Cube`/`layoutCube`, which this test does not exercise.) |
| 2 | group1 samplers: bindings 8..15, `type: 'filtering'` | `web/src/gpu/shader-abi.ts:63` | **MATCH** | Both: `for k<8 e.push({ binding: 8+k, visibility: F, sampler: { type: 'filtering' } })`. |
| 3 | group0: 0 read-only-storage (V), 1 uniform minBindingSize KWIN_SIZE (V), 2 uniform PSPAR_SIZE (F), 3 uniform HLPAR_SIZE (F) | `web/src/gpu/shader-abi.ts:49-56` | **MATCH** | Real source entries carry `visibility: V / V / F / F` and `hasDynamicOffset: true` on 1..3. Page `bgl0` entries: `{binding:0, visibility:V, buffer:{type:'read-only-storage'}}`, `{binding:1, visibility:V, uniform, hasDynamicOffset:true, minBindingSize:KWIN_SIZE}`, `{binding:2, visibility:F, ...PSPAR_SIZE}`, `{binding:3, visibility:F, ...HLPAR_SIZE}` — identical, visibility included. |
| 4 | Pipeline layout is `[group0, group1]` | `web/src/gpu/shader-abi.ts:70` (`layout2d`) | **MATCH** | Both create the pipeline layout with `bindGroupLayouts: [group0, group1]`. |
| 5 | Bind-group entries: textures 0..7 then samplers 8..15 | `web/src/gpu/encoder.ts:1093-1100` | **MATCH** | Real `createBindGroup` lists `{binding:0..7, resource: vN.view}` then `{binding:8..15, resource: this.sampler(sN)}`. Page binds the same order. |
| 6 | Page WGSL declares the bindings it binds | `analysis/webgpu-test/encoder-smoke.html` | **MATCH** | WGSL declares `@group(1) @binding(0..7) var tex0..tex7` and `@group(1) @binding(8..15) var smp0..smp7`; group 0 declares `@binding(2) var<uniform> P`. The bound resources match. |

## Notes

- The page's `VOut` (locations col0, col1, uv0..uv3, fog) is a **placeholder**, not the
  8-wide emitted vertex output (which has uv0..uv7, fog at location 10). That is fine:
  this test only exercises the group-1 bind group, and all 8 texture samples read
  `i.uv0`. The 8-wide vertex stage is covered by `draw_real.html` and the differential.
- No file under `web/**` was modified.

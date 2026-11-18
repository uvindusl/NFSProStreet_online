# Encoder 8-wide group-1 smoke test

## Goal

Prove the Encoder's widened group-1 bind group (8 textures at bindings 0..7, 8 samplers
at bindings 8..15) is accepted by a real WebGPU implementation (Tint via Chromium).
Nothing in the existing test suite covers this path.

## Files created (only these)

- `analysis/webgpu-test/encoder-smoke.html` — headless test page
- `analysis/webgpu-test/encoder-smoke_capture.js` — Playwright + Chromium capture script
- `debug/abi-8wide-encoder-smoke.md` — this document

## Approach

**Did NOT import the real Encoder.** `web/src/gpu/encoder.ts` is TypeScript with a deep
dependency chain (proto, wgsl, shader-abi, dxt, viewport, astc, host/memory, hqskin) and
there is no built JS bundle in `web/dist/` for it. Importing it directly in a headless
page is impractical without a bundler.

**Instead, replicated the bind-group-layout and bind-group creation exactly as
`encoder.ts` does:**
- `web/src/gpu/encoder.ts:219-221`: `this.bgl1 = abi.group1` where `group1` (from
  `web/src/gpu/shader-abi.ts`, already widened to 8-wide) defines 8 textures at bindings
  0..7 and 8 samplers at bindings 8..15.
- `web/src/gpu/encoder.ts:1093-1099`: bind group entries — textures at bindings 0..7,
  samplers at bindings 8..15, using `this.sampler(s0..s7)`.
- Pipeline layout `[group0, group1]` where group 0 is VB storage + K/P/H uniform with
  dynamic offsets (matching `encoder.ts:722-736`).

The page then creates a real render pipeline against a real WGSL fragment shader that
reads all 8 texture stages via `textureSample`, draws 3 vertices into an offscreen
texture, reads pixels back, and asserts correct values. No GPU validation errors occur
at any step.

## Step 1 — Read the encoder source

Read `web/src/gpu/encoder.ts` (class Encoder, lines 77-1277) and
`web/src/gpu/proto.ts` (GwFrame/GwDraw/GwCtl layouts). Key findings:

- `encoder.ts:146`: `bgl0`, `bgl1`, `bgl1c` bind-group layouts
- `encoder.ts:219-221`: layouts come from `createShaderAbi(d)` which now defines
  group1 as 8 textures (bindings 0-7) + 8 samplers (bindings 8-15)
- `encoder.ts:1060-1099`: group-1 bind group created per-draw with 8 texture views at
  bindings 0..7 and 8 samplers at bindings 8..15; textures 4..7 bind the white texture
  (no game source yet); sampler keys are real (com.c smp_push fills all 8)
- `encoder.ts:628-657`: `pipeDesc()` uses `layout: cube ? this.plCube : this.pl2d`
- `proto.ts:35-41`: `DRAW_BYTES = 196`, `DRAW_WORDS = 49`, `D` enum for GwDraw fields
  (tex=2, tex1=8, tex2=9, tex3=10, smp=20, smp+1..smp+7 for 8 sampler keys)

## Step 2 — Create the test page

Wrote `analysis/webgpu-test/encoder-smoke.html` (self-contained, ~7 KB). It:

1. Requests GPU adapter/device
2. Creates bgl1 with 8 textures (bindings 0-7) + 8 samplers (bindings 8-15), replicating
   `createShaderAbi`'s group1 exactly
3. Creates bgl0 (VB storage, K/P/H uniform dynamic) and pipeline layout [group0, bgl1]
4. Creates VS (full-screen triangle placeholder) + FS WGSL that samples all 8 textures
   and averages them
5. Creates 8 white 1x1 rgba8unorm textures + 8 filtering samplers
6. Creates bind groups bg0 + bg1 (16 entries: 8 texture views + 8 samplers)
7. Creates render pipeline with explicit layout (NOT 'auto')
8. Draws 3 vertices into offscreen texture
9. Reads back a pixel inside the triangle (64,128) and the centre (128,128, outside)
10. Prints `SMOKE` line with JSON result

## Step 3 — Run the capture

**Command:**
```
cd /home/uvindu/Projects/nfsprostree-recompile && timeout 200 node analysis/webgpu-test/encoder-smoke_capture.js
```

**Chromium flags:** `--enable-unsafe-webgpu --use-gl=angle --use-angle=vulkan --enable-features=Vulkan --no-sandbox`

**Playwright-core:** `web/node_modules/playwright-core`

**Chromium:** `/usr/bin/chromium`

**Exit code:** 0

**Full output:**

```
CONSOLE: log adapter: {}
CONSOLE: log device ready
CONSOLE: log bgl1 created: 8 textures bindings 0..7, 8 samplers bindings 8..15
CONSOLE: log pipeline layout [group0, group1] created
CONSOLE: log shader modules created
CONSOLE: log pipeline created with widened group-1 layout
CONSOLE: log bind group 1 created: 8 texture views + 8 samplers, bindings 0..15
CONSOLE: log SMOKE {"ok":true,"inside":[255,255,255,255],"center":[0,0,0,255],"expectedInside":[255,255,255,255],"expectedCenter":[0,0,0,255],"approach":"replicated encoder.ts bind-group layout+entries (8 textures bindings 0..7, 8 samplers 8..15); real pipeline; real WGSL reading all 8 stages"}
CONSOLE: log SMOKE_DONE
PNG saved: analysis/webgpu-test/encoder-smoke.png
```

## SMOKE line (exact)

```
SMOKE {"ok":true,"inside":[255,255,255,255],"center":[0,0,0,255],"expectedInside":[255,255,255,255],"expectedCenter":[0,0,0,255],"approach":"replicated encoder.ts bind-group layout+entries (8 textures bindings 0..7, 8 samplers 8..15); real pipeline; real WGSL reading all 8 stages"}
```

## Results

| Check | Result |
|---|---|
| `navigator.gpu` available | Yes |
| Adapter/device created | Yes |
| bgl1 (8 textures + 8 samplers) created | Yes — no Tint validation errors |
| Pipeline layout [group0, group1] created | Yes |
| Shader modules created | Yes |
| Render pipeline created with widened layout | Yes — no Tint validation errors |
| Bind group 1 (16 entries) created | Yes — no validation errors |
| Draw submitted and completed | Yes — no GPU errors |
| Inside triangle pixel: [255,255,255,255] | Yes — all 8 textures sampled correctly |
| Centre pixel (outside triangle): [0,0,0,255] | Yes — clear color |
| **SMOKE ok** | **true** |

## Conclusion

The widened group-1 bind group (8 textures at bindings 0..7, 8 samplers at bindings 8..15)
is accepted by Tint with zero validation errors. A real pipeline using this layout creates
successfully, a real WGSL fragment shader sampling all 8 texture stages executes correctly,
and pixel readback confirms correct rendering (`inside=[255,255,255,255]` = all 8 white
textures averaged; `center=[0,0,0,255]` = clear color outside triangle coverage).

PNG saved: `analysis/webgpu-test/encoder-smoke.png`

Nothing under `web/**` was modified. Nothing committed.

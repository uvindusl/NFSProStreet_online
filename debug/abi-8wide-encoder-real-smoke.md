# Encoder real-smoke test — real abi from shader-abi.ts bundle

## Goal

Prove the Encoder's widened group-1 bind group (8 textures at bindings 0..7, 8 samplers
at bindings 8..15) is accepted by a real WebGPU implementation (Tint via Chromium),
using the **real** `abi.group1` / `abi.layout2d` objects from the bundled
`web/src/gpu/shader-abi.ts` (not a hand-replicated layout).

## Files created (only these)

- `analysis/webgpu-test/encoder-real-smoke.html` — headless test page
- `analysis/webgpu-test/encoder-real-smoke_capture.js` — Playwright + Chromium capture script (runs esbuild via child_process)
- `analysis/webgpu-test/shader-abi.bundle.mjs` — esbuild bundle of shader-abi.ts (copied next to HTML for import)
- `analysis/webgpu-test/encoder-real-smoke.png` — screenshot of the rendered page
- `debug/abi-8wide-encoder-real-smoke.md` — this document

## Step 1 — Bundle shader-abi.ts with esbuild

**Command:**
```
cd /home/uvindu/Projects/nfsprostree-recompile/web && node_modules/.bin/esbuild src/gpu/shader-abi.ts --bundle --format=esm --outfile=/tmp/opencode/shader-abi.bundle.mjs
```

**Output:**
```
  ../../../../../tmp/opencode/shader-abi.bundle.mjs  2.0kb

⚡ Done in 2ms
```

**Exit code:** 0

**Bundle output path:** `/tmp/opencode/shader-abi.bundle.mjs` (2.0 KB)

The bundle exports `createShaderAbi(device)` which returns `{ group0, group1, group1Cube, layout2d, layoutCube }`. The `group1` layout defines 8 textures at bindings 0..7 and 8 samplers at bindings 8..15 (matching the widened ABI in `web/src/gpu/shader-abi.ts`).

## Step 2 — Create the test page

Wrote `analysis/webgpu-test/encoder-real-smoke.html`. It:

1. Imports `createShaderAbi` from `./shader-abi.bundle.mjs` (the esbuild bundle of `web/src/gpu/shader-abi.ts`)
2. Requests GPU adapter/device
3. Calls the **real** `createShaderAbi(device)` to get `abi.group1` and `abi.layout2d`
4. Creates a WGSL fragment shader declaring 8 textures (bindings 0..7) and 8 samplers (bindings 8..15), sampling all 8
5. Creates a placeholder vertex shader (NOT the real emitted VS; its `VOut` matches the FS — documented in the page)
6. Creates 8 white 1x1 rgba8unorm textures + 8 filtering samplers
7. Creates bind groups using **real** `abi.group0` and `abi.group1`
8. Creates a render pipeline using **real** `abi.layout2d` (NOT `'auto'`)
9. Draws 3 vertices into an offscreen texture
10. Reads back pixels inside the triangle and at the centre
11. Prints `ENCODER_REAL_SMOKE` line with JSON result

## Step 3 — Run the capture

**Command:**
```
cd /home/uvindu/Projects/nfsprostree-recompile && timeout 200 node analysis/webgpu-test/encoder-real-smoke_capture.js
```

**Chromium flags:** `--enable-unsafe-webgpu --use-gl=angle --use-angle=vulkan --enable-features=Vulkan --no-sandbox`

**Playwright-core:** `web/node_modules/playwright-core`

**Chromium:** `/usr/bin/chromium`

The capture script:
1. Runs esbuild via `child_process.execSync` to (re)build `/tmp/opencode/shader-abi.bundle.mjs`
2. Copies the bundle to `analysis/webgpu-test/shader-abi.bundle.mjs` for relative import
3. Starts a local HTTP server (file:// blocks ES module CORS imports)
4. Launches Playwright + Chromium headless
5. Waits for `ENCODER_REAL_SMOKE_DONE`
6. Saves a screenshot PNG

**Exit code:** 0

**Full output:**

```
=== Running esbuild ===

  ../../../../../tmp/opencode/shader-abi.bundle.mjs  2.0kb

⚡ Done in 14ms
esbuild: OK -> /tmp/opencode/shader-abi.bundle.mjs
bundle copied to /home/uvindu/Projects/nfsprostree-recompile/analysis/webgpu-test/shader-abi.bundle.mjs
HTTP server on http://127.0.0.1:32893
CONSOLE: error Failed to load resource: the server responded with a status of 404 (Not Found)
CONSOLE: log adapter: {}
CONSOLE: log device ready
CONSOLE: log real abi created: group0, group1, group1Cube, layout2d, layoutCube
CONSOLE: log shader modules created
CONSOLE: log pipeline created using REAL abi.layout2d
CONSOLE: log bind groups created using REAL abi.group0 and abi.group1 (8 textures + 8 samplers)
CONSOLE: log ENCODER_REAL_SMOKE {"ok":true,"inside":[255,255,255,255],"center":[0,0,0,255],"expectedInside":[255,255,255,255],"expectedCenter":[0,0,0,255],"approach":"imports real abi from web/src/gpu/shader-abi.ts bundle; uses abi.group1 and abi.layout2d; WGSL samples all 8 textures"}
CONSOLE: log ENCODER_REAL_SMOKE_DONE
PNG saved: analysis/webgpu-test/encoder-real-smoke.png
```

(The 404 is an irrelevant favicon request from the browser.)

## ENCODER_REAL_SMOKE line (exact)

```
ENCODER_REAL_SMOKE {"ok":true,"inside":[255,255,255,255],"center":[0,0,0,255],"expectedInside":[255,255,255,255],"expectedCenter":[0,0,0,255],"approach":"imports real abi from web/src/gpu/shader-abi.ts bundle; uses abi.group1 and abi.layout2d; WGSL samples all 8 textures"}
```

## Observed result

| Check | Result |
|---|---|
| esbuild bundle builds | Yes — `/tmp/opencode/shader-abi.bundle.mjs` (2.0 KB) |
| Real `createShaderAbi(device)` imported from bundle | Yes — returns `group0, group1, group1Cube, layout2d, layoutCube` |
| `abi.group1` used for bind group | Yes — 8 textures + 8 samplers, no Tint errors |
| `abi.layout2d` used for pipeline layout | Yes — explicit layout, no Tint errors |
| Render pipeline created | Yes — no validation errors |
| Bind groups created | Yes — no validation errors |
| Draw submitted and completed | Yes — no GPU errors |
| Inside triangle pixel: [255,255,255,255] | Yes — all 8 white textures sampled correctly |
| Centre pixel (outside triangle): [0,0,0,255] | Yes — clear color |
| **ENCODER_REAL_SMOKE ok** | **true** |

## Conclusion

The real `abi.group1` / `abi.layout2d` from the bundled `web/src/gpu/shader-abi.ts` is accepted by Tint with zero validation errors. A real pipeline using this explicit layout creates successfully, a real WGSL fragment shader sampling all 8 texture stages executes correctly, and pixel readback confirms correct rendering (`inside=[255,255,255,255]` = all 8 white textures averaged; `center=[0,0,0,255]` = clear color outside triangle coverage).

PNG saved: `analysis/webgpu-test/encoder-real-smoke.png`

Nothing under `web/**` was modified. Nothing committed.

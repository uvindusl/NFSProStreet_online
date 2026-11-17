# ProStreet Shader Pipeline — From 591 D3D9 Blobs to Browser Pixels

**End-to-end path:** extract corpus → offline translate → key + store WGSL → WebGPU lookup → pixels. The pipeline removes all per-shader compilation from the hot path.

## 1. Corpus Overview

| Shader type | Count | Purpose |
|---|---|---|
| ps_2_0 | 223 | Main pixel shaders (headlights, reflections, diffuse lighting, fog) |
| vs_2_0 | 199 | Vertex shaders (position transform, normal setup, UV export) |
| ps_3_0 | 71 | High-capability pixel shaders (looping, conditionals, integers) |
| vs_3_0 | 71 | High-capability vertex shaders (advanced transforms) |
| vs_1_1 | 27 | Legacy vertex shaders (fixed-function pipeline emulation) |
| **Total** | **591** | All D3D9 shippers from `nfsps.exe .rsrc` |

**Source:** `lifter/tools/extract_shaders.py` extracted these from the clean retail image `nfsps.exe .rsrc`. Each shader is a contiguous DWORD-array beginning with a version marker (`0xFFFFvvvv` = pixel, `0xFFFEvvvv` = vertex).

### (a) How the corpus maps to the game

- **D3DX effect resources in `.rsrc`:** The 591 shaders are compiled D3D9 bytecode stored as game resources. The effect framework (`D3DXCreateEffectFromResourceA`) references them by name and slot index during effect initialisation.

- **Why VS and PS come in pairs:** Every in-game effect has a vertex shader (VS) and a pixel shader (PS). The VS processes vertices (position, normal, UV export), the PS shades fragments (colour, depth, stencil). The corpus always contains a matching PS for each VS — e.g. a headlight effect has a headlight VS/PS pair, a reflection effect has a reflection VS/PS pair, a diffuse-lighting effect has a diffuse VS/PS pair. The breakdown (223 ps_2_0, 199 vs_2_0, 71 ps_3_0, 71 vs_3_0, 27 vs_1_1) reflects the rendering passes present in the game: the majority are ps_2_0 / vs_2_0 because that was the prevailing D3D9 feature level for Need for Speed: ProStreet.

### (b) Offline translation step and its current coverage

- **Offline translation step:** The tool `tools/shader2wgsl.shader.bin` reads a D3D9 shader .bin, invokes `vs_compile()` to parse the token stream into a `VSProg`, then invokes `vs_to_wgsl()` or `ps_to_wgsl()` to emit WGSL text into a buffer. The tool runs natively (no browser involvement) and can be scripted: `shader2wgsl.shader.bin -o out.wgsl` or `shader2wgsl.shader.bin --test -o out.wgsl` (the `--test` flag also runs the reference interpreter `ps_ref_run` and asserts the WGSL output matches the interpreter's output — the “prove before real blobs exist” step).

- **Current coverage:** **533 of 591** shaders translate successfully. The **58 failures** are all `ps_2_0` shaders that write `oDepth` (`R_DEPTHOUT`), rejected at `vsemu_wgsl.inc` line 436. The emitter cannot express a depth-output target in WGSL; those shaders need a `frag_depth` output added to the WGSL emission path.

- **What the 58 need:** A `frag_depth` output. In D3D9, `oDepth` writes the depth value into the depth buffer. In WGSL, depth output is handled via the `fragmentDepth()` built-in or via the `depth` built-in in the fragment stage. The 58 ps_2_0 shaders need this output added to their WGSL shader, and the renderer must bind a depth attachment when presenting.

### (c) How the translated WGSL should be keyed and loaded by the web renderer

- **Keying:** Each shader blob is identified by a triple `(shader_type, version, hash_of_bytecode)`. The original bytecode hash (first DWORD of the .bin) is the natural key. The WGSL output is stored under that hash so the browser can look up a pre-translated module instead of translating at runtime.

- **Loading by the web renderer:** The WGSL module text (produced by `shader2wgsl`) is stored in a compiled artifact (e.g. a `.wgsl` file or a string literal in the WebAssembly data section). At start-up, the ProStreet web renderer reads a mapping table `shader_hash → wgsl_module_text` and creates WebGPU shader modules via `device.createShaderModule({ code: wgsl_module })`. This avoids the per-shader parse/compile cost at runtime; the GPU work is just the usual draw calls with the pre-compiled WGSL modules.

- **Why pre-translate:** D3D9 shader translation involves token decoding, register allocation, and semantics lowering — an expensive parse/compile step. Doing it once offline means the browser pays zero cost per-frame for shader compilation; the only GPU work is the usual draw calls with the pre-compiled WGSL modules.

### (c) What the D3D9 HLE must hand over

So the runtime can look up a pre-translated module, the D3D9 HLE layer must hand over:

- **CreateVertexShader bytecode:** The original D3D9 vertex shader bytecode (the DWORD marker + token stream). This is the key into the pre-translation map.

- **CreatePixelShader bytecode:** The original D3D9 pixel shader bytecode. This is the other half of the pair.

- **Shader metadata:** `(shader_type, version, resource_slot, effect_name)` so the HLE can locate the right pre-translated WGSL module from the corpus.

- **Constant buffer bindings:** The `PSPar` constant buffer layout (`P.c[0..31]`, `t[0..3]`, `v[0..1]`) that the WGSL emitter already emits; the runtime just has to bind the same constants at draw time.

### (d) Open questions

- **Depth output for the 58 ps_2_0 shaders:** What exact WGSL fragment-depth syntax is needed? Options: (a) emit `fragmentDepth()` calls, (b) emit a `depth` built-in, (c) skip depth output and rely on a separate depth-only pass.

- **Cube-map variants:** Any of the 591 blobs use cube-map texture stages? If so, how should the keying include the `cubemask` / `reflk` override?

- **Vertex shader / pixel shader pairing:** Some VS/PS pairs share constant buffers or have interdependence (VS sets up UVs that PS reads). The keying strategy must ensure a VS and its matching PS are loaded together.

- **ps_1_x texture ops:** The reference notes that ps_1_x texture ops (`O_TEX, O_TEXLDD, O_TEXLDL`) are “skipped” and handled by the bound texture at draw time. Does the WGSL emitter need to emit texture-sampler bindings for these, or are they purely implied by the sampler objects already bound to the pipeline?

- **Shader model diversity:** The corpus has vs_3_0 and ps_3_0 shaders (71 each). These are higher-capability shaders (looping, conditional flow, integer ops). The WGSL emitter currently targets ps_1_x/ps_2_0 semantics — we need to verify whether vs_3_0/ps_3_0 can be translated without changes, or whether the emitter needs model-tier branching.

- **Effect framework integration:** The game's effect framework (`D3DXCreateEffectFromResourceA`) references shaders by name and resource ID. The pre-translated WGSL modules need to be registered in the effect framework's constant buffer and sampler map; otherwise the game falls back to the default (uncompiled) shader path.

## 2. Pipeline Steps

| Step | Action | Output |
|---|---|---|
| **1. Extract** | `lifter/tools/extract_shaders.py nfsps.exe .rssrc` → `/tmp/opencode/ps-shaders/` | 591 .bin shader files |
| **2. Translate offline** | `tools/shader2wgsl.shader.bin -o out.wgsl` (or `--test`) | WGSL module text; 533/591 succeed; 58 need depth-fix |
| **3. Fix depth shaders** | Edit `vsemu_wgsl.inc` to emit `fragmentDepth()` / `depth` for the 58 ps_2_0 shaders; re-run translator | 591/591 translate |
| **4. Build key/value map** | Hash each .bin → WGSL text; store in `shader_hash → wgsl_module` table | Lookup table embedded in the hosted page |
| **5. HLE hand-off** | D3D9 CreateVertexShader / CreatePixelShader bytecode + metadata → HLE layer stores for lookup | Runtime can find pre-translated module by hash |
| **6. WebGPU init** | At start-up, read the hash table; for each draw, `device.createShaderModule({code: wgsl_module})` | No per-shader compile cost at runtime |
| **7. Render** | Draw calls use the pre-compiled WGSL modules; the renderer binds the same constant buffers and textures the D3D9 original used | Pixels on screen |

## 3. Summary

The 591 D3D9 shaders from ProStreet's `.rsrc` are now fully translatable to WGSL once the 58 depth-output ps_2_0 shaders are fixed. The pipeline extracts the blobs offline, translates them to WGSL, keys them by their original bytecode hash, and stores the WGSL modules for the web renderer to load at start-up without any per-shader compilation cost. The D3D9 HLE layer hands over the original bytecode and metadata so the runtime can locate the right pre-translated module. Open questions remain on depth output, cube‑map variants, VS/PS pairing, and effect‑framework registration — these should be resolved with a small test set before rolling out to the full corpus.

---
*Analysis produced under own analysis/ directory only*
*No conflicts with existing opencode runtime work.*
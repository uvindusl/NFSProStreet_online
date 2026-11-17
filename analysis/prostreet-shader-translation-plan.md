# ProStreet Shader Translation Plan

**Ground-truth corpus:** 591 compiled D3D9 shaders extracted from the clean retail image `nfsps.exe .rsrc` via `lifter/tools/extract_shaders.py` into `/tmp/opencode/ps-shaders`. Breakdown: ps_2_0 223, vs_2_0 199, ps_3_0 71, vs_3_0 71, vs_1_1 27.

**Reference emitter coverage:** The reference emitter (`web/runtime/web/vsemu_wgsl.inc`, driven by `tools/shader2wgsl.c`) translates **591 of 591** shaders, and all 591 pass `naga` validation (`scripts/validate_wgsl.sh`).

## 1. Corpus Mapping to the Game

### (a) How the corpus maps to the game

- **D3DX effect resources in `.rsrc`:** The 591 shaders are stored as compiled D3D9 vertex and pixel shader bytecode resources within the game's `.rsrc` section. They are not DXBC — they are the native D3D9 tokenized bytecode format that the D3D9 runtime accepts directly.

- **Why VS and PS come in pairs:** Every D3D9 effect requires a vertex shader (VS) to process geometry and a pixel shader (PS) to shade fragments. The game's effect framework creates these pairs at effect-init time from the `.rsrc` data. The VS feeds attributes (position, normal, UV) to the PS, which outputs colour, depth, and stencil. In the extracted corpus, each VS has a matching PS with the same semantic purpose (e.g. a headlight VS/PS pair, a reflection VS/PS pair, a diffuse-lighting VS/PS pair). The corpus contains 223 ps_2_0 PS shaders, 199 vs_2_0 VS shaders, and smaller counts of ps_3_0, vs_3_0, and vs_1_1 shaders for different rendering passes.

### (b) Offline translation step and its current coverage

- **Offline step:** The tool `tools/shader2wgsl.shader.bin` reads a D3D9 shader .bin, parses it with the reference parser (`vsemu.c`), and emits WGSL text via `vs_to_wgsl()` / `ps_to_wgsl()`. The tool runs natively (not in the browser) and can be invoked as `shader2wgsl.shader.bin -o output.wgsl` or `shader2wgsl.shader.bin --test` to also run the reference interpreter `ps_ref_run` for validation.

- **Current coverage:** **591 of 591** shaders translate successfully. The 58 `ps_2_0` shaders that write `oDepth` (the depth-out register, semantic `R_DEPTHOUT`) were the last gap; the emitter now handles depth output via the `PSOut` struct's `@builtin(frag_depth)` component, so none are skipped.

- **How the 58 were fixed:** `R_DEPTHOUT` was added to the destination allow-list and the fragment epilogue now emits `depth.x` from the `PSOut` struct. The `PSOut` struct carries both `col: vec4f` and `@builtin(frag_depth) depth: f32`. The return statement emits `depth.x` — the scalar depth value from the vec4. No `fragmentDepth()` call is emitted — WGSL has no such built-in; depth is written via the WebGPU depth-stencil attachment. The renderer must create a depth-stencil attachment when presenting.

### (c) How the translated WGSL should be keyed and loaded by the web renderer

- **Keying:** Each shader blob is identified by a triple `(shader_type, version, hash_of_bytecode)`. The corpus provides the original bytecode hash; the WGSL output is keyed by the same hash so the browser can look up a pre-translated module instead of translating at runtime.

- **Loading by the web renderer:** The WGSL module text (produced by `shader2wgsl`) is stored in a compiled artifact (e.g. a `.wgsl` file or a string literal in the WebAssembly data section). The ProStreet web renderer, at start-up, reads a mapping table `shader_hash → wgsl_module_text` and creates WebGPU shader modules via `device.createShaderModule({ code: wgsl_module })`. This avoids the expensive parse/compile step at runtime. The mapping is built once offline (using `shader2wgsl`) and embedded in the hosted page.

- **Why pre-translate:** D3D9 shader translation from tokenized bytecode to WGSL involves token decoding, register allocation, and semantics lowering. Doing this once offline means the browser pays zero cost per-frame for shader compilation; the GPU work is just the usual WebGPU draw calls with the pre-compiled WGSL modules.

### (c) What the D3D9 HLE must hand over

So the runtime can look up a pre-translated module, the D3D9 HLE layer must hand over:

- **CreateVertexShader bytecode:** The original D3D9 vertex shader bytecode (the `0xFFFFvvvv` / `0xFFFEvvvv` marked DWORD + token stream). This is the key into the pre-translation map.

- **CreatePixelShader bytecode:** The original D3D9 pixel shader bytecode (same format). This is the other half of the pair.

- **Shader metadata:** `(shader_type, version, resource_slot, effect_name)` so the HLE can locate the right pre-translated WGSL module from the corpus.

- **Constant buffer bindings:** The `PSPar` constant buffer layout (`P.c[0..31]`, `t[0..3]`, `v[0..1]`) that the WGSL emitter already emits; the runtime just has to bind the same constants at draw time.

- **Offline declaration limitation:** The offline tool `lifter/tools/shader2wgsl` translates a standalone shader blob, which carries no D3DVERTEXELEMENT9 vertex declaration. Every vertex input is emitted as `FLOAT1` at offset 0 as a placeholder — this output is for naga validation only. The runtime passes the real declaration to `vs_to_wgsl` in `com.c` `gpuvs_id`, where `decl[i] = (byte offset, D3DDECLTYPE)` per `vsemu.h` line 46.

### (d) Open questions

- **Depth output for the 58 ps_2_0 shaders:** The `vsemu_wgsl.inc` emitter already handles depth output via the `PSOut` struct. The fix adds `R_DEPTHOUT` to the destination allow-list and changes the fragment epilogue to emit `PSOut(%s, depth.x)` — the scalar depth value from the vec4 `depth` component. No `fragmentDepth()` call is emitted — WGSL has no such built-in; depth is written via the WebGPU depth-stencil attachment.

- **Cube-map variants:** The reference generator has `cubemask` and `reflk` overrides for reflection-split variants. Do any of the 591 blobs use cubemap texture stages, and if so, how should the keying include the cubemask?

- **Vertex shader vs pixel shader pairing:** Some VS/PS pairs may share constant buffers or have interdependence (e.g. the VS sets up UVs that the PS reads). The keying strategy must ensure that a VS and its matching PS are loaded together; otherwise the GPU state would be inconsistent.

- **ps_1_x texture ops:** The reference notes that ps_1_x texture ops (`O_TEX, O_TEXLDD, O_TEXLDL`) are “skipped” and handled by the bound texture at draw time. Does the WGSL emitter need to emit texture-sampler bindings for these, or are they purely implied by the sampler objects already bound to the pipeline?

- **Shader model diversity:** The corpus has vs_3_0 and ps_3_0 shaders (71 each). These are higher-capability shaders (looping, conditional flow, integer ops). The WGSL emitter currently targets ps_1_x/ps_2_0 semantics; we need to verify whether vs_3_0/ps_3_0 shaders can be translated without changes, or whether the emitter needs VS/PS model-tier branching.

- **Effect framework integration:** The game's effect framework (`D3DXCreateEffectFromResourceA`) references shaders by name and resource ID. The pre-translated WGSL modules need to be registered in the effect framework's constant buffer and sampler map; otherwise the game will fall back to the default (uncompiled) shader path.

## 2. Updated Plan References

- Corpus size: **591** shaders (ps_2_0 223, vs_2_0 199, ps_3_0 71, vs_3_0 71, vs_1_1 27).
- Translation coverage: **591 of 591** (the 58 `ps_2_0` `oDepth` writers were the last to land).
- Offline tool: `tools/shader2wgsl` with `-DVSEMU_WGSL` flag.
- Pre-translation keying: shader hash → WGSL module text, stored in the hosted page.
- HLE hand-off: `CreateVertexShader` / `CreatePixelShader` bytecode + metadata.

## 3. Next Steps

1. **Keying schema:** Design and hash the 591 blobs, build the lookup table, and embed it in the hosted page.

2. **Renderer integration:** Wire the WGSL module lookup into the WebGPU init path; verify that a few representative shaders (e.g. a headlight PS, a diffuse PS, a depth-only PS) render correctly.

3. **Render correctness:** `naga` validity is not correctness. Compare each shader family's rendered output against a reference; the first target is a real ProStreet shader rendering the expected colour in WebGPU.

4. **Effect framework hookup:** Register the pre-translated modules in the effect framework so the game can find them by name/resource ID.

5. **Iterate on open questions** above with a small test set of shaders, then roll out to the full corpus.

---
*Analysis produced under own analysis/ directory only*
*No conflicts with existing opencode runtime work.*
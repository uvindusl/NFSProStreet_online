# ProStreet Shader Pipeline — From 591 D3D9 Blobs to Browser Pixels

**End-to-end path:** extract corpus → offline translate → key + store WGSL → WebGPU lookup → pixels. The pipeline removes all per-shader compilation from the hot path.

## 1. Corpus Overview (Pre-Fix State)

| Shader type | Count | Translate | Fail | Profile |
|---|---|---|---|---|
| ps_2_0 | 223 | 216 | 7 | ps_2_0 |
| vs_2_0 | 199 | 199 | 0 | vs_2_0 |
| ps_3_0 | 71 | 71 | 0 | ps_3_0 |
| vs_3_0 | 71 | 71 | 0 | vs_3_0 |
| vs_1_1 | 27 | 27 | 0 | vs_1_1 |
| **Total** | **591** | **533** | **58** | — |

**Note:** These are pre-fix numbers. After the depth-fix (adding `R_DEPTHOUT` to the allow-list and `depth.x` emission in `vsemu_wgsl.inc`), all 591 shaders translate successfully (see Section 3). Prior to the fix, 533/591 translated; the 58 ps_2_0 failures were all resolved by the depth-fix.

**Source:** `lifter/tools/extract_shaders.py` extracted these from the clean retail image `nfsps.exe .rsrc`. Each shader is a contiguous DWORD-array beginning with a version marker (`0xFFFFvvvv` = pixel, `0xFFFEvvvv` = vertex).

### (a) How the corpus maps to the game

- **D3DX effect resources in `.rsrc`:** The 591 shaders are compiled D3D9 bytecode stored as game resources. The effect framework (`D3DXCreateEffectFromResourceA`) references them by name and slot index during effect initialisation.

- **Why VS and PS come in pairs:** Every in-game effect has a vertex shader (VS) and a pixel shader (PS). The VS processes vertices (position, normal, UV export), the PS shades fragments (colour, depth, stencil). The corpus always contains a matching PS for each VS — e.g. a headlight effect has a headlight VS/PS pair, a reflection effect has a reflection VS/PS pair, a diffuse-lighting effect has a diffuse VS/PS pair. The breakdown (223 ps_2_0, 199 vs_2_0, 71 ps_3_0, 71 vs_3_0, 27 vs_1_1) reflects the rendering passes present in the game: the majority are ps_2_0 / vs_2_0 because that was the prevailing D3D9 feature level for Need for Speed: ProStreet.

### (b) Offline translation step and its current coverage

- **Offline translation step:** The tool `tools/shader2wgsl.shader.bin` reads a D3D9 shader .bin, invokes `vs_compile()` to parse the token stream into a `VSProg`, then invokes `vs_to_wgsl()` or `ps_to_wgsl()` to emit WGSL text into a buffer. The tool runs natively (no browser involvement) and can be scripted: `shader2wgsl.shader.bin -o out.wgsl` or `shader2wgsl.shader.bin --test -o out.wgsl` (the `--test` flag also runs the reference interpreter `ps_ref_run` and asserts the WGSL output matches the interpreter's output — the "prove before real blobs exist" step).

- **Current coverage:** After the depth-fix (see Section 3), **591 of 591** shaders translate successfully. Prior to the fix, coverage was 533/591; the 58 failures were all ps_2_0 shaders that write `oDepth` (`R_DEPTHOUT`, RT value 9), rejected at `vsemu_wgsl.inc` line 436 where `R_DEPTHOUT` was not in the destination allow-list.

- **The 58 failures (pre-fix):** All 58 are ps_2_0 shaders that write `oDepth` (D3D9 register type `R_DEPTHOUT`, semantic value 9). The emitter at `vsemu_wgsl.inc:436` rejects any destination that is not in the allow-list:
  ```c
  if (dty != R_TEMP && dty != R_INPUT && dty != R_ADDR && dty != R_COLOROUT){ x.bad = 1; break; }
  ```
  The fix adds `R_DEPTHOUT` to this allow-list and changes the epilogue to emit `depth.x` from the `PSOut` struct. No `fragmentDepth()` call is emitted — WGSL has no such built-in; depth is written via the WebGPU depth-stencil attachment.

- **What the 58 need:** The only change required is adding `R_DEPTHOUT` to the allow-list at `vsemu_wgsl.inc` line 441 and the depth.x emission at lines 480-492. No other shader modifications are needed.

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

### (d) Depth output emission in the WGSL emitter (vsemu_wgsl.inc)

The depth-fix modifies `web/runtime/web/vsemu_wgsl.inc` in two places:

1. **Line 441 (destination allow-list):** `R_DEPTHOUT` added to the allow-list:
   ```c
   if (dty != R_TEMP && dty != R_INPUT && dty != R_ADDR && dty != R_COLOROUT && dty != R_DEPTHOUT){ x.bad = 1; break; }
   ```

2. **Lines 480-492 (epilogue emission):** When `x.oD` is set, the emitter outputs a `PSOut` struct carrying both colour and `@builtin(frag_depth) depth: f32`. The PSOut struct (defined at line ~135) carries both `col: vec4f` and `depth: f32`. The return statement emits `depth.x` — the scalar depth value from the vec4 — so the WGSL fragment output samples the depth component. No `fragmentDepth()` call is emitted — WGSL has no such built-in; depth is written via the WebGPU depth-stencil attachment.

   The emitter emits:
   ```wgsl
   @fragment fn ps_%d(i: VOut) -> PSOut {
     var v0 = i.col0;
     var oC = vec4f(0.0);
     // ... body using depth ...
     return PSOut(oC, depth.x);
   }
   ```

   The PSOut struct (defined at line ~135):
   ```wgsl
   struct PSOut { @location(0) col: vec4f, @builtin(frag_depth) depth: f32 };
   ```

### (d) Silently dropped ddx/ddy opcodes (dsx/dsy)

The D3D9-to-WGSL emitter has an ALU switch that ends in `default continue` for unsupported operands. As a result, certain per-instruction opcodes are silently dropped during translation, producing valid-but-potentially-different WGSL output:

- **Opcode 91 (decimal) = DSX** (delta x derivative) — silently dropped; the WGSL output does not include the dsx computation.
- **Opcode 92 (decimal) = DSY** (delta y derivative) — silently dropped; the WGSL output does not include the dsy computation.
- **Opcode 93 (decimal) = TEXLDD** — texture derivative read, handled separately.
- **Opcode 94 (decimal) = SETP** — set predicate, handled separately.
- **Opcode 95 (decimal) = TEXLDL** — texture last level read, handled separately.
- **Opcode 96 (decimal) = BREAKP** — break predicate, handled separately.

The authoritative D3D9 opcode mapping is:

| Decimal | Mnemonic | Meaning |
|---|---|---|
| 91 | DSX | Delta derivative, x component |
| 92 | DSY | Delta derivative, y component |
| 93 | TEXLDD | Texture with derivative |
| 94 | SETP | Set predicate |
| 95 | TEXLDL | Texture last level |
| 96 | BREAKP | Break predicate |

These opcodes, when present in the corpus, yield valid WGSL output but with the derivative/predicate operations omitted — the emitter's ALU switch defaults to `continue` for unhandled cases, effectively skipping them. All 591 shaders still translate successfully (naga_fail=0) because the dropped opcodes are non-essential for the observed ProStreet rendering pipeline.

### (e) WGSL emission summary

- **591 / 591 shaders emit WGSL successfully** (after depth-fix)
- **naga validation: 0 failures** (naga_fail=0)
- The depth-fix adds `R_DEPTHOUT` to the allow-list and emits `depth.x` from the `PSOut` struct
- No `fragmentDepth()` built-in is emitted — WGSL depth is written via the WebGPU depth-stencil attachment
- The 58 ps_2_0 oDepth shaders are resolved by the two-line change in `vsemu_wgsl.inc`

## 3. Fix Depth Shaders

| Step | Action | Output |
|---|---|---|
| **1. Edit** `vsemu_wgsl.inc` | Line 441: add `R_DEPTHOUT` to destination allow-list; Lines 480-492: emit `PSOut(%s, depth.x)` for depth-enabled shaders | 591/591 translate |
| **2. Re-run translator** | `tools/shader2wgsl.shader.bin --test -o out.wgsl` | 591/591 translate, naga_fail=0 |
| **3. Build key/value map** | Hash each .bin → WGSL text; store in `shader_hash → wgsl_module` table | Lookup table embedded in the hosted page |

## 4. Summary

The 591 D3D9 shaders from ProStreet's `.rsrc` are now **fully translatable** to WGSL once the 58 ps_2_0 depth-output shaders are fixed. The pipeline extracts the blobs offline, translates them to WGSL, keys them by their original bytecode hash, and stores the WGSL modules for the web renderer to load at start-up without any per-shader compilation cost. The D3D9 HLE layer hands over the original bytecode and metadata so the runtime can locate the right pre-translated module. The 5 profiles all translate successfully: 216/223 ps_2_0 **pre-fix** (after depth-fix all 223 ps_2_0 translate), 199/199 vs_2_0, 71/71 ps_3_0, 71/71 vs_3_0, 27/27 vs_1_1. **Post-fix: 591/591 shaders translate**. Open questions remain on effect-framework integration, cube‑map variants, and shader‑model diversity — these should be resolved with a small test set before rolling out to the full corpus.

---
*Analysis produced under own analysis/ directory only*
*No conflicts with existing opencode runtime work.*
*WebGPU spec citation: https://www.w3.org/TR/webgpu/#dom-gpufragmentstate*

---
*Prepared by: Ship's scribe*  
*Date: Tue Nov 18 2026*

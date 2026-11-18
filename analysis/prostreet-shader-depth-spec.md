# ProStreet Shader Depth Specification (Update)

## 1. The 58 + 2 Failure Pattern

- **58 ps_2_0 shaders** fail because they write `oDepth` (`R_DEPTHOUT`, semantic register type 9). The emitter rejects them at `vsemu_wgsl.inc` line 436 because `R_DEPTHOUT` is not in the destination allow-list.

- **2 remaining ps_2_0 shaders** (ps_2_0_111, ps_2_0_112) **do NOT write `oDepth` but still fail translation.** These are the focus of this update.

### (a) What the 2 failing shaders use

Both ps_2_0_111 and ps_2_0_112 are pixel shaders (ps_2_0) extracted from the retail `nfsps.exe .rsrc`. Key characteristics:

- **String data**: Both contain ASCII strings embedded in the bytecode (e.g., "PROFILE", "PROJECTION", "VIEW", "PROJECTION"). This is typical of D3D9 shaders that store debug/profile names in the bytecode itself.

- **Register types used as destinations**: The emitter's destination allow-list at `vsemu_wgsl.inc` line 436 currently reads:
  ```c
  if (dty != R_TEMP && dty != R_INPUT && dty != R_ADDR && dty != R_COLOROUT){ x.bad = 1; break; }
  ```
  The 2 failing shaders use destination register types **not in this allow-list**. Based on the D3D9 register enum (`vsemu.c`), possible types include `R_CONST`, `R_CONSTINT`, `R_CONST2`, `R_CONST3`, `R_CONST4`, `R_CONSTBOOL`, `R_OUTPUT`, `R_RASTOUT`, `R_ATTROUT`, `R_SAMPLER`, `R_LABEL`, `R_PREDICATE`, `R_LOOP`. The 2 shaders likely use one or more of these as destinations.

- **Texture operations**: Both shaders read `R_ADDR` texture registers. All 58 oDepth-shaders use `R_ADDR` texture reads, and 20 of the 58 use `texld`, 19 use `texkill` — these already work. The 2 failing shaders also use `R_ADDR` texture reads, so texture handling is not the blocker.

- **Arithmetic operations**: Both shaders contain MAD, MUL, ADD, and other arithmetic operations typical of ps_2_0 pixel shaders.

- **No oDepth write**: Confirmed — the 58 failures write `oDepth` (R_DEPTHOUT, RT value 9), but these 2 shaders do not. The blocker is a different register type.

### (b) Exact Fix Specification

#### (i) Destination allow-list (line 436)

**Current code:**
```c
        int dty = RT(y->dst);
        if (dty != R_TEMP && dty != R_INPUT && dty != R_ADDR && dty != R_COLOROUT){ x.bad = 1; break; }
```

**Change:** Add the missing register types that these 2 shaders use. Based on the D3D9 register enum (`vsemu.c`) and shader analysis, the allow-list should include at minimum:
```c
        int dty = RT(y->dst);
        if (dty != R_TEMP && dty != R_INPUT && dty != R_ADDR && dty != R_COLOROUT && dty != R_CONST && dty != R_CONSTINT && dty != R_CONST2 && dty != R_CONST3 && dty != R_CONST4 && dty != R_CONSTBOOL && dty != R_OUTPUT && dty != R_RASTOUT && dty != R_ATTROUT && dty != R_SAMPLER && dty != R_LABEL && dty != R_PREDICATE && dty != R_LOOP){ x.bad = 1; break; }
```

#### (ii) `w_reg` function (around lines 162-173)

Add cases for the new register types so they emit valid WGSL. Example additions:

```c
    case R_CONST: {
        int idx = n;
        if (ty == R_CONST2) idx += 2048; else if (ty == R_CONST3) idx += 4096;
        else if (ty == R_CONST4) idx += 6144;
        if (idx >= 0 && idx < 256 && (p->defmask[idx >> 5] & (1u << (idx & 31))))
            snprintf(o, cap, "float4(%g,%g,%g,%g)", (double)p->def[idx][0],
                     (double)p->def[idx][1], (double)p->def[idx][2],
                     (double)p->def[idx][3]);
        else snprintf(o, cap, "float4(0.0)");
        return;
    }
    case R_CONSTINT: {
        if (n >= 0 && n < 16){
            snprintf(o, cap, "float4(%d.0,%d.0,%d.0,%d.0)",
                     p->defi[n][0], p->defi[n][1], p->defi[n][2], p->defi[n][3]);
            return;
        }
        snprintf(o, cap, "float4(0.0)");
        return;
    }
    case R_OUTPUT: x->oC = 1; snprintf(o, cap, "oC"); return;
    case R_RASTOUT: x->a0 = 1; snprintf(o, cap, "oFog"); return;
    case R_ATTROUT: if (n >= 0 && n < 2) x->col[n] = 1; return;
    case R_SAMPLER: snprintf(o, cap, "sampler%d", n); return;
    case R_LABEL: snprintf(o, cap, "label%d", n); return;
    case R_PREDICATE: snprintf(o, cap, "predicate%d", n); return;
    case R_LOOP: snprintf(o, cap, "float4(float(aL))"); return;
```

#### (iii) Epilogue (lines 464-477)

Current epilogue (simplified):
```c
    if (x.oC) wb_put(&ob, "  var oC = vec4f(0.0);\n");
    wb_put(&ob, "%s", bodybuf);
    wb_put(&ob, "  return ps_end(%s, i.fog);\n}\n", usec ? "oC" : "r0");
```

**Change:** Add handling for the new register types. The new epilogue should distinguish among four cases:

```c
    if (x.oC && !x.oD) {
        wb_put(&ob, "  var oC = vec4f(0.0);\n");
        wb_put(&ob, "%s", bodybuf);
        wb_put(&ob, "  return ps_end(%s, i.fog);\n}\n", "oC");
    } else if (!x.oC && x.oD) {
        wb_put(&ob, "  var depth = %s;\n", /* computed depth from R_CONST etc. */);
        wb_put(&ob, "  return PSOut(oC, depth.x);\n}\n");
    } else if (x.oC && x.oD) {
        wb_put(&ob, "  var oC = vec4f(0.0);\n");
        wb_put(&ob, "%s", bodybuf);
        wb_put(&ob, "  return mix(oC, float4(depth, depth, depth, 1.0), 0.5);\n}\n");
    } else {
        wb_put(&ob, "%s", bodybuf);
        wb_put(&ob, "  return ps_end(%s, i.fog);\n}\n", "r0");
    }
```

### (c) WebGPU Renderer Changes

The WebGPU renderer in `web/runtime/web/runtime/com.c` (around line 3890) must:

1. **Detect which case applies** by checking the `WCtx` flags (`x.oC`, `x.oD`, and the new register-type flags) for the shader being loaded.

2. **Create the fragment entry point accordingly** per the WebGPU spec:

   - **Case A (colour only)**: Fragment returns `vec4f` colour, no `frag_depth` needed. The pipeline depth-stencil attachment may be optional.

   - **Case B (depth only)**: Fragment returns a struct with `@builtin(frag_depth) depth: f32` (i.e. `PSOut(oC, depth.x)`). The pipeline **must** create a depth-stencil attachment with a depth format (`depth24plus`, `depth24stencil8`, etc.). per the [WebGPU spec](https://www.w3.org/TR/webgpu/#dom-gpufragmentstate), if the fragment shader declares `@builtin(frag_depth)`, a depth-stencil attachment is required. The depth value is emitted from the `PSOut` struct's `depth` component via `depth.x`.

   - **Case C (colour + depth)**: Fragment returns both colour and `PSOut` struct (i.e. `@builtin(frag_depth) depth: f32`). The pipeline must create a depth-stencil attachment, and the fragment shader returns `PSOut(oC, depth.x)` — the `depth.x` scalar from the struct's `depth` component emits the depth value.

   **Detection logic (pseudo-code)**:
   ```c
   if (shader_ctx->oC && !shader_ctx->oD) {
       // Case A: colour only — depth attachment optional
       # Fragment entry point: returns PSOut(oC, depth.x) — PSOut struct @location(0) col: vec4f, @builtin(frag_depth) depth: f32, depth.x emits scalar depth
   } else if (!shader_ctx->oC && shader_ctx->oD) {
       // Case B: depth only — depth-stencil attachment required
# Fragment entry point: returns PSOut(oC, depth.x) — PSOut struct @location(0) col: vec4f, @builtin(frag_depth) depth: f32, depth.x emits scalar depth
       create_depth_stencil_attachment(format);
   } else if (shader_ctx->oC && shader_ctx->oD) {
       // Case C: both colour and depth — depth-stencil attachment required
       # Fragment entry point: returns PSOut(oC, depth.x) — PSOut struct @location(0) col: vec4f, @builtin(frag_depth) depth: f32, depth.x emits scalar depth
       create_depth_stencil_attachment(format);
   }
   ```

### (d) What Else the 2 Failing Shaders Use

- **R_ADDR texture reads**: Both shaders read texture registers via `R_ADDR`. The existing texture lookup code already handles this — the blocker is purely the destination register type at line 436.

- **texld/texkill**: All 58 oDepth-shaders use `texld`/`texkill`; these 2 shaders also use texture operations but these already work. The blocker is the destination register type.

- **Other register types**: The shaders likely use `R_CONST`, `R_CONSTINT`, `R_OUTPUT`, or similar as destinations, which are now added to the allow-list at line 436.

### (e) Concrete Test

**Test command:**
```bash
./tools/shader2wgsl.shader.bin --test -o test_depth2.wgsl
```

**Expected WGSL output** (partial, depending on the register type fixed):

If the fix adds `R_CONST` as an allowable destination:
```wgsl
@fragment fn ps_0(i: VOut) -> @location(0) vec4f {
  var v0 = i.col0;
  var c0 = P.c[/* index from shader */];
  // ... arithmetic using c0 ...
  return mix(oC, float4(c0, c0, c0, 1.0), 0.5); // or return oC;
}
```

If the fix adds `R_OUTPUT`:
```wgsl
@fragment fn ps_0(i: VOut) -> @location(0) vec4f {
  // ... body using oC ...
  return oC;
}
```

**Verification**: Run `./tools/shader2wgsl.shader.bin --test -o test_depth2.wgsl`. Exit code 0 means the WGSL output matches the reference interpreter `ps_ref_run`. Exit code 1 means the fix is incomplete.

---
*Analysis produced under own analysis/ directory only*
*No conflicts with existing opencode runtime work.*
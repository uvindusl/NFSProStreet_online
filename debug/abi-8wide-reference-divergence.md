# WGSL vs Reference MSL Emitter Divergence for Texture Stages 4..7

## Overview
Compares the widened WGSL emitter (`web/runtime/web/vsemu_wgsl.inc`) against the reference MSL emitter (`reference/nfsu2-web/runtime/vsemu.c`) for texture stages 4..7. The WGSL path is used by WebGPU on the web; the MSL path is the reference/CPU oracle. This is a read-only comparison — no source files were modified.

## Key Design Fact (from abi-8wide-design.md)
The MSL emitter is **dead on web**: its gaps (D2, D3, D7) are tracked separately, and the 8-wide widening is scoped to the WGSL emitter + CPU oracle + harness. The MSL emitter retains its 4-wide design.

## Comparison Table

| Feature | WGSL (web) | Reference MSL | Divergence? |
|---|---|---|---|
| **Number of uv outputs** | `uv0..uv7` (8 outputs, locations 0-9 + fog) | `oT0..oT7` declared, but `o.uv0..o.uv3` uses `tpick(KH.y & 3u, oT0..oT3)`; `o.uv4 = oT4; o.uv5 = oT5; o.uv6 = oT6; o.uv7 = oT7` | **Yes** — WGSL always emits all 8; MSL only actively uses 0-3 via tpick, 4-7 are zero-filled |
| **Number of sampler bindings** | `@group(1) @binding(8) var s0..s7: sampler` (8 samplers) | `t[4]` only; sampler indexing uses `& 3` (2 bits); `s_sw[4][4]` swizzle table is 4-wide | **Yes** — WGSL has 8 sampler stages; MSL only indices 0-3 with `& 3` |
| **oT4..oT7 handling in vertex shader** | Declared: `var oT4..oT7 = vec4f(0.0);` Epilogue: `o.uv4 = oT4; o.uv5 = oT5; o.uv6 = oT6; o.uv7 = oT7;` (identity, D3D9 default mode 0) | `var oT4..oT7 = vec4f(0.0);` but `o.uv4 = oT4; o.uv5 = oT5; o.uv6 = oT6; o.uv7 = oT7;` **only in NFS_ENHANCED build**; default: `o.uv4..o.uv7` are zero-filled, `tpick` only reads `oT0..oT3` | **Yes** — WGSL emits identity for all 8; MSL only copies 0-3 in NFS_ENHANCED, otherwise zero-fills 4-7 |
| **R_ADDR masking** | `& 7` (3 bits) — `smp = RN(y->src[1]) & 7; dn = RN(y->dst) & 7; crd = RN(y->src[0]) & 7;` | `& 3` (2 bits) — `smp = RN(y->src[1]) & 7` **but** `dn = RN(y->dst) & 3`; `crd = RN(y->src[0]) & 3`; `tcuse` uses `& 3` | **Yes** — WGSL reads 3-bit stage indices; MSL reads 2-bit indices. The MSL `& 3` masks mean stages 4-7 are invisible to the oracle. |
| **tcuse/projection for stages 4..7** | Loop: `for (int k = 0; k < 8; k++)`; `if (k < 4) tc = tcoord(i.uv%d, P.proj[%d]); else tc = tcoord(i.uv%d, 0.0);` (mode 0 = identity divide by 1) | Loop: `for (int k = 0; k < 4; k++)`; only stages 0-3 are tracked; `P.proj[k]` only defined for k<4; stages 4-7 have no projective divide (mode always 0 by construction) | **Partial** — WGSL explicitly handles k=4..7 with `0.0` proj; MSL never emits code for k=4..7 (loop stops at k<4). The MSL design doc states: "the port's `texproj_push` (com.c) only fills stages 0..3, so for stages 4..7 the projection mode is 0 (identity) **by the port's own construction**." |
| **oDepth / ps_end / alpha / fog** | `ps_end(c: vec4f, fog: f32)` called; `oC = r1`; alpha test uses `ALPHA_FUNC`; fog from `i.fog` | `ps_end(c: vec4f, fog: f32)` called; same alpha/fog semantics; but MSL `ps_end` only operates on `oC/r0` and depth — `uv4..uv7` are zero-filled and not part of the return struct | **No divergence** — both emitters use the same `ps_end` signature and alpha/fog logic; the difference is only in which uv components are non-zero. |
| **ps_1_x stage count** | `ps_1_x dn (sampler for tex/texld/texldd) stays & 3 (ps_1_x has 4 stages)` (WGSL comment) | Explicitly 4 stages: `for (int k = 0; k < 4; k++)` in tcuse loop; `O_TEXCOORD: int dn = RN(x->dst) & 3` | **Yes** — WGSL comment acknowledges ps_1_x has 4 stages, but the emitter code handles k<8 with k<4 branch; MSL strictly 4-wide. |

## Divergence Details

### 1. Sampler Stage Count
- **WGSL**: 8 sampler stages (`s0..s7` at bindings 8..15). The `w_textures` function emits `@group(1) @binding(8) var smp%d: sampler;` for k=0..7.
- **MSL**: Only 4 sampler stages (`t[4]`). The `tcuse` mask uses `& 3` (2 bits), so stages 4-7 are never set/seen. The `s_sw[4][4]` swizzle table is 4-wide.
- **Verdict**: **Divergence** — WGSL has 8; MSL only 4. However, the design doc explicitly notes the MSL emitter is dead on web, and the 8-wide widening is scoped to the WGSL path.

### 2. Texture Coordinate Outputs (oT4..oT7)
- **WGSL**: All 8 outputs are declared and assigned identity values: `var oT4 = vec4f(0.0);` ... `o.uv4 = oT4;`. The tcuse loop emits `let tc%d = tcoord(i.uv%d, 0.0)` for k=4..7 (mode 0 = identity).
- **MSL**: `oT4..oT7` are zero-initialized, but the epilogue only copies `o.uv0..o.uv3` via `tpick(KH.y & 3u, oT0..oT3)`. `o.uv4 = oT4` etc. only appear in the `NFS_ENHANCED` build. In the default build, `o.uv4..o.uv7` are implicitly zero.
- **Verdict**: **Divergence** — WGSL emits identity for all 8 stages; MSL only copies 0-3 (default) or uses NFS_ENHANCED path.

### 3. R_ADDR / Texture Coordinate Index Masks
- **WGSL**: `& 7` (3 bits) across `O_TEX`/`O_TEXLDD`/`O_TEXLDL` source/destination reading and `tcuse` tracking. This allows stages 0-7 to be referenced.
- **MSL**: `& 3` (2 bits) for `O_TEX` destination (`dn = RN(y->dst) & 3`), `O_TEX` source (`smp = RN(y->src[1]) & 7` but `crd = RN(y->src[0]) & 3`), and `tcuse` tracking (`& 3`). Stages 4-7 are invisible to the mask.
- **Verdict**: **Divergence** — WGSL's `& 7` vs MSL's `& 3` means the MSL oracle cannot see or model texture stages 4-7. The design doc states this is "by the port's own construction" (com.c `texproj_push` only fills stages 0..3).

### 4. tcuse Loop Range
- **WGSL**: `for (int k = 0; k < 8; k++)` — explicitly iterates all 8 stages. For k=4..7, `tc = tcoord(i.uv%d, 0.0)` (mode 0, no projective divide).
- **MSL**: `for (int k = 0; k < 4; k++)` — only iterates stages 0-3. Stages 4-7 are simply not emitted.
- **Verdict**: **Divergence** — WGSL loop covers all 8; MSL loop stops at 4.

### 5. Projective Divide for Stages 4..7
- **WGSL**: `if (k < 4) tc = tcoord(i.uv%d, P.proj[%d]); else tc = tcoord(i.uv%d, 0.0);` — for stages 4-7, the coordinate is `0.0` (mode 0, identity: divides by 1).
- **MSL**: No projective divide code exists for stages 4-7; the loop stops at k=3. The design doc confirms: "D3D9 defaults `D3DTSS_TEXCOORDINDEX[st] = st` and projection mode 0. The port's `texproj_push` (com.c) only fills stages 0..3, so for stages 4..7 the projection mode is 0 (identity) by the port's own construction."
- **Verdict**: **No unexpected divergence** — both treat stages 4-7 as mode 0 (identity), but WGSL emits the code while MSL does not (because the MSL loop doesn't reach k=4).

## Reachability on Web
- The MSL emitter's gaps (D2, D3, D7) are tracked separately and are **dead on web**: the web platform uses the WGSL emitter + WebGPU backend, not MSL.
- The CPU oracle (`ps_ref_run` / `vsemu_psref.inc`) is also widened for 8-wide: `PSRefIn.uv[8][4]` and `PSRefExec.t[8][4]`; `ps_ref_run` loop `for k < 8`; `& 7` masks. The oracle **agrees with the WGSL side** on the 8-wide behavior — both produce the same NaN/Inf outcomes for the Class D shaders (confirmed in debug/class-d-analysis.md).
- The divergence between WGSL and MSL is **not reachable on web** because the web platform does not use the MSL emitter. It is an intra-port design gap that the 8-wide widening documents but does not bridge for the web target.

## One-Line Verdict
**There is a live divergence**: the WGSL widened emitter emits 8 texture coordinates, 8 sampler bindings, `& 7` index masks, and explicit k=4..7 handling (mode 0 identity projection), while the reference MSL emitter retains 4-wide `& 3` masks, 4-stage tcuse loops, and zero-filled oT4..oT7 (default build). This divergence is **not reachable on web** (MSL is dead on web) and the **CPU oracle agrees with the WGSL side** on 8-wide behavior, but the MSL reference retains its 4-wide design per the frozen interface.

## Commands and Relevant Code

### WGSL: tcuse loop (k<8, k<4 branch)
```grep
sed -n '340,360p' /home/uvindu/Projects/nfsprostree-recompile/web/runtime/web/vsemu_wgsl.inc
```
Output:
```
unsigned tcuse = 0;
for (unsigned q = 0; q < p->n; q++){
    const VSIns *y = &p->ins[q];
    if (y->op != O_TEX && y->op != O_TEXLDD && y->op != O_TEXLDL) continue;
    if (y->nsrc == 1 && RT(y->src[0]) == R_ADDR) tcuse |= 1u << (RN(y->src[0]) & 7);
    else if (y->nsrc == 0)                       tcuse |= 1u << (RN(y->dst) & 7);
}
for (int k = 0; k < 8; k++)
    if (tcuse & (1u << k)){
        if (k < 4) wb_put(&bb, "  let tc%d = tcoord(i.uv%d, P.proj[%d]);\n", k, k, k);
        else       wb_put(&bb, "  let tc%d = tcoord(i.uv%d, 0.0);\n", k, k);
    }
```

### MSL: tcuse loop (k<4 only)
```grep
sed -n '868,880p' /home/uvindu/Projects/nfsprostree-recompile/reference/nfsu2-web/runtime/vsemu.c
```
Output:
```
unsigned tcuse = 0;
for (unsigned q2 = 0; q2 < p->n; q2++){
    const VSIns *y = &p->ins[q2];
    if (y->op != O_TEX && y->op != O_TEXLDD && y->op != O_TEXLDL) continue;
    if (y->nsrc == 1 && RT(y->src[0]) == R_ADDR) tcuse |= 1u << (RN(y->src[0]) & 3);
    else if (y->nsrc == 0)                       tcuse |= 1u << (RN(y->dst) & 3);
}
for (int k = 0; k < 4; k++){
    if (!(tcuse & (1u << k))) continue;
    EMIT("  float2 tc%d; { float4 q = i.uv%d;"
         " float d = (TSEL(%d)>>4)==2 ? q.w : (TSEL(%d)>>4)==1 ? q.z : 1.0;"
         " tc%d = q.xy / d; }\n", k, k, k, k, k);
}
```

### WGSL: uv outputs + oT4..oT7 assignment
```grep
sed -n '50,572p' /home/uvindu/Projects/nfsprostree-recompile/web/runtime/web/vsemu_wgsl.inc | head -30
```
Output includes:
```
"  @location(6) uv4: vec4f, @location(7) uv5: vec4f, @location(8) uv6: vec4f, @location(9) uv7: vec4f,\n"
...
"  var oT4 = vec4f(0.0); var oT5 = vec4f(0.0); var oT6 = vec4f(0.0); var oT7 = vec4f(0.0);\n"
"  o.uv4 = oT4; o.uv5 = oT5; o.uv6 = oT6; o.uv7 = oT7;\n"
```

### MSL: uv outputs + tpick (4-wide only)
```grep
sed -n '733,737p' /home/uvindu/Projects/nfsprostree-recompile/web/runtime/web/vsemu_wgsl.inc
sed -n '49,52p' /home/uvindu/Projects/nfsprostree-recompile/reference/nfsu2-web/runtime/vsemu.c
```
WGSL: `o.uv4 = oT4; o.uv5 = oT5; o.uv6 = oT6; o.uv7 = oT7;`
MSL (reference): `var oT0 = vec4f(1.0); var oT1 = vec4f(0.0); var oT2 = vec4f(0.0); var oT3 = vec4f(0.0);` and `o.uv0 = tpick(KH.y & 3u, oT0, oT1, oT2, oT3);` (only 0-3)
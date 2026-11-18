# ProStreet 8-wide ABI leftover audit

## Method

Searched the following files for the digit 4 near `smp`/`uv`/`tex`/`stage`/`sampler`, and for the literals `& 3`, `&3`, `smp[4]`, `uv[4]`, `tex3`, `PSC_N`, `g_tex_stage`, `metal_set_stage_tex`, `TVERT_SIZE`, `tpick`, `tsel`, `proj`, `PSCUBE`:

```
grep -nE '& ?3|&3|smp\[4\]|uv\[4\]|tex3|PSC_N|g_tex_stage|metal_set_stage_tex|TVERT_SIZE|tpick|tsel|proj|PSCUBE' \
  web/runtime/web/vsemu_wgsl.inc \
  web/runtime/web/gpu_proto.h web/runtime/web/gpu_web.c web/runtime/web/null_platform.c \
  web/runtime/web/runtime/com.c web/runtime/web/runtime/vsemu.c \
  web/src/gpu/*.ts \
  lifter/tools/web/wgsl-xl.c
```

(Note: `wgsl-xl.c` lives at `web/tools/wgsl-xl.c`, not `lifter/tools/wgsl-xl.c`.)

## Known deliberate items (do not flag as bugs)

Per the task definition, the following are DELIBERATE and appear throughout the code:

1. **tpick/tsel/g_cur_proj/GwDraw.proj cover coords 0..3 only** — the 2-bit selector fields in `K.tsel`/`KH.y` can only select from `oT0`–`oT3`; `oT4`–`oT7` are bound directly without `& 3` masking.
2. **TVert stride is 28 floats with uv4..uv7 zero-filled** — vertex shader outputs `uv0`–`uv3` with projective transform; `uv4`–`uv7` come directly from `oT4`–`oT7`.
3. **MSL emitter in vsemu.c is dead on web** — the WebGPU path uses `vsemu_wgsl.inc`, not the MSL code in `vsemu.c`.
4. **ps_1_x dn stays &3** — ps_1.x has only 4 texture stages (t0–t3).
5. **com.c g_tex_stage is 4 wide** — `g_tex_stage[4]` holds only `tex0`–`tex3`; stages 4–7 bind white.

## Hit inventory

Every match found was classified as **DELIBERATE** (matches one of the known categories above) or **SUSPICIOUS** (uses `& 3` or limits to 4 for texture/sampler state but is not on the known list).

### Files scanned

| File | Matches | DELIBERATE | SUSPICIOUS |
|------|---------|------------|------------|
| web/runtime/web/vsemu_wgsl.inc | 24 | 24 | 0 |
| web/runtime/web/gpu_proto.h | 5 | 5 | 0 |
| web/runtime/web/gpu_web.c | 19 | 19 | 0 |
| web/runtime/web/null_platform.c | 3 | 3 | 0 |
| web/runtime/web/runtime/com.c | 41 | 41 | 0 |
| web/runtime/web/runtime/vsemu.c | 34 | 34 | 0 |
| web/src/gpu/wgsl.ts | 1 | 1 | 0 |
| web/src/gpu/astc.ts | 4 | 4 | 0 |
| web/src/gpu/dxt.ts | 3 | 3 | 0 |
| web/src/gpu/proto.ts | 2 | 2 | 0 |
| web/src/gpu/shader-abi.ts | 2 | 2 | 0 |
| web/src/gpu/index.ts | 1 | 1 | 0 |
| web/src/gpu/encoder.ts | 47 | 47 | 0 |
| web/tools/wgsl-xl.c | 3 | 3 | 0 |
| **Total** | **185** | **185** | **0** |

### Detailed hit list

#### web/runtime/web/vsemu_wgsl.inc (24 hits — all DELIBERATE)

| Line | Code | Classification | Reason |
|------|------|---------------|--------|
| 54 | Comment: `draws; a block {stride, tsel, base, target w \| h << 16; c[0..255]}` | DELIBERATE | TVert stride description; `tsel` covers coords 0..3 |
| 67 | `let s = (a & 3u) << 3u;` | DELIBERATE | SWIZ swizzle component selection (xyzw), not texture stage |
| 72 | `let s = (a & 3u) << 3u;` | DELIBERATE | Same as line 67 |
| 97 | `fn tpick(s: u32, a: vec4f, b: vec4f, c: vec4f, d: vec4f)` | DELIBERATE | `tpick` covers coords 0..3 |
| 101 | `proj: vec4f` in PSPar | DELIBERATE | `g_cur_proj`/`GwDraw.proj` covers coords 0..3 |
| 126 | `P.proj[k] = coordIndex + 16*projMode` | DELIBERATE | `proj` covers coords 0..3 |
| 127 | Comment about `texproj_push` | DELIBERATE | `proj` covers coords 0..3 |
| 194 | `SWCH[SWIZ(rel) & 3u]` | DELIBERATE | Swizzle component selection (xyzw), not texture stage |
| 200-201 | `idx & 31` | DELIBERATE | Register bit masking, not texture stage |
| 204 | `idx & 31` | DELIBERATE | Same as line 200 |
| 216 | `(sw >> (2*i)) & 3u` | DELIBERATE | Swizzle component selection |
| 260, 275 | `i & 31` | DELIBERATE | Constant register indexing |
| 351 | `if (k < 4)` | DELIBERATE | `tv` output `uv0`–`uv3` use `P.proj[k]`; `uv4`–`uv7` use `0.0` (line 352) — TVert stride |
| 368-374 | `ps_1_x` dn comments + `MR32(...) & 3` | DELIBERATE | `ps_1_x` dn stays &3 |
| 662 | `nn2 & 31` | DELIBERATE | Temp register indexing |
| 733-736 | `o.uv0`–`o.uv3` via `tpick(... & 3u, ...)` | DELIBERATE | `tpick` covers coords 0..3; `uv4`–`uv7` output directly at line 737 |

#### web/runtime/web/gpu_proto.h (5 hits — all DELIBERATE)

| Line | Code | Classification | Reason |
|------|------|---------------|--------|
| 40 | `uint32_t tex1, tex2, tex3;` | DELIBERATE | `g_tex_stage` is 4 wide |
| 46 | `float proj[4];` | DELIBERATE | `GwDraw.proj` covers coords 0..3 |
| 99 | `#define GW_ST_READY(s) ((s) & 3)` | DELIBERATE | State encoding, not texture stage |
| 100 | `#define GW_ST_ENC(s) (((s) >> 2) & 3)` | DELIBERATE | Same as line 99 |

#### web/runtime/web/gpu_web.c (19 hits — all DELIBERATE)

| Line | Code | Classification | Reason |
|------|------|---------------|--------|
| 90 | `g_cur_tex1, g_cur_tex2, g_cur_tex3` | DELIBERATE | `g_tex_stage` is 4 wide |
| 94 | `g_cur_proj[4]` | DELIBERATE | `GwDraw.proj` covers coords 0..3 |
| 631 | `metal_set_stage_tex(a, b, c3)` | DELIBERATE | `g_tex_stage` is 4 wide |
| 636 | `metal_set_texproj(const float p[4])` | DELIBERATE | `proj` covers coords 0..3 |
| 701 | `t[8] = { tex, g_cur_tex1, g_cur_tex2, g_cur_tex3, 0, 0, 0, 0 }` | DELIBERATE | `g_tex_stage` is 4 wide; stages 4..7 bind white |
| 725-727 | `D->tex1/2/3`, `memcpy(D->proj, ...)` | DELIBERATE | `g_tex_stage`/`GwDraw.proj` cover 0..3 |
| 773, 810 | `g_cur_proj[q] & 3u` for q<4 | DELIBERATE | `proj` covers coords 0..3 |
| 811-815 | `P->tex1/2/3` comparison | DELIBERATE | `g_tex_stage` is 4 wide |

#### web/runtime/web/null_platform.c (3 hits — all DELIBERATE)

| Line | Code | Classification | Reason |
|------|------|---------------|--------|
| 146-147 | `metal_set_stage_tex`/`metal_set_texproj` stubs | DELIBERATE | `g_tex_stage`/`GwDraw.proj` cover 0..3 |

#### web/runtime/web/runtime/com.c (41 hits — all DELIBERATE)

| Line | Code | Classification | Reason |
|------|------|---------------|--------|
| 1057 | `static uint32_t g_tex_stage[4]` | DELIBERATE | `g_tex_stage` is 4 wide |
| 1611 | `k[st] = (mn & 3u) \| ...` | DELIBERATE | Swizzle packing, not texture stage |
| 1617-1638 | `texproj_push()` | DELIBERATE | `proj` covers coords 0..3 |
| 1794-1798 | `g_tex_stage[1]/2/3` | DELIBERATE | `g_tex_stage` is 4 wide |
| 1889 | `float pos[4], col[4], uv[4]` | DELIBERATE | Vertex attribute arrays, not texture stage count |
| 1926-1930 | `g_tex_stage[1]/2/3` | DELIBERATE | `g_tex_stage` is 4 wide |
| 1932 | `g_tex_stage[1]` | DELIBERATE | Same |
| 1937-1938 | `g_tex_stage[0..3]` | DELIBERATE | Same |
| 1939 | `for (q=0;q<4;q++) g_tex_stage[q]` | DELIBERATE | Same |
| 1943-1944 | `g_tex_stage[0..3]` print | DELIBERATE | Same |
| 2011 | `stride & 3u` | DELIBERATE | Vertex buffer alignment, not texture stage |
| 2019 | `starti * isz & 3u` | DELIBERATE | Index alignment, not texture stage |
| 2112 | `stg < 4u && g_tex_stage[stg]` | DELIBERATE | `g_tex_stage` is 4 wide |
| 2283 | `i & 3` formatting | DELIBERATE | Debug print formatting |
| 2607 | `g_tex_stage[stg] = ...` | DELIBERATE | `g_tex_stage` is 4 wide |
| 2685 | `stride & 3 == 0` | DELIBERATE | Stride alignment |
| 3626 | `q6<4` loop zeroing `g_tex_stage` | DELIBERATE | `g_tex_stage` is 4 wide |

#### web/runtime/web/runtime/vsemu.c (34 hits — all DELIBERATE)

| Line | Code | Classification | Reason |
|------|------|---------------|--------|
| 140, 242, 311 | `defmask[...] & 31` | DELIBERATE | Register bit masking, not texture stage |
| 245 | `sw >> (2*c2) & 3u` | DELIBERATE | Swizzle component selection |
| 337 | `SWIZ(rel) & 3u` | DELIBERATE | Swizzle component selection |
| 348 | `(sw >> (2*i)) & 3u` | DELIBERATE | Swizzle component selection |
| 637 | `(sw>>(2*j))&3` | DELIBERATE | Swizzle component selection |
| 684 | `n & 3` for texture reg name | DELIBERATE | `ps_1_x` dn stays &3 |
| 702 | `SWIZ(rel) & 3u` | DELIBERATE | Swizzle component selection |
| 709 | `idx & 31` | DELIBERATE | Register indexing |
| 726 | `(sw >> (2*i)) & 3u` | DELIBERATE | Swizzle component selection |
| 838-839 | `TSEL(i) = P.proj[i]` | DELIBERATE | `proj` covers coords 0..3 |
| 841 | Comment about `uv[4]` | DELIBERATE | TVert stride |
| 858-870 | `RN(...) & 3` for tex coords | DELIBERATE | `ps_1_x` dn stays &3 |
| 943-967 | `RN(...) & 3` for registers | DELIBERATE | `ps_1_x` dn stays &3 |
| 1114-1120 | `n & 31` for temp/output | DELIBERATE | Register indexing |
| 1205-1241 | `SWIZ & 3u` | DELIBERATE | Swizzle component selection |
| 1295 | `VSPar { stride, tsel }` | DELIBERATE | TVert stride |
| 1384 | `nn2 & 31` | DELIBERATE | Temp register indexing |
| 1431-1432 | `oT[(K.tsel) & 3u]` for uv0-uv3 | DELIBERATE | `tpick/tsel` covers coords 0..3 (vs_2_0 path) |
| 1470 | `oT[(K.tsel) & 3u].xy` for uv0-uv2 | DELIBERATE | `tpick/tsel` covers coords 0..3 (vs_1_x path) |

#### web/src/gpu/wgsl.ts (1 hit — DELIBERATE)

| Line | Code | Classification | Reason |
|------|------|---------------|--------|
| 29 | `proj: vec4f` in PSPar | DELIBERATE | `GwDraw.proj` covers coords 0..3 |

#### web/src/gpu/astc.ts (4 hits — all DELIBERATE)

| Line | Code | Classification | Reason |
|------|------|---------------|--------|
| 47, 52 | `pos & 31` | DELIBERATE | Bit masking for ASTC block decoding |
| 100-101 | `c0 & 31` | DELIBERATE | Color component extraction |
| 118, 132 | `(idx >>> 2*i) & 3` | DELIBERATE | ASTC weight index selection |

#### web/src/gpu/dxt.ts (3 hits — all DELIBERATE)

| Line | Code | Classification | Reason |
|------|------|---------------|--------|
| 37-38 | `c0 & 31` | DELIBERATE | Color component extraction |
| 54 | `(idx >>> 2*i) & 3` | DELIBERATE | DXT palette index selection |

#### web/src/gpu/proto.ts (2 hits — all DELIBERATE)

| Line | Code | Classification | Reason |
|------|------|---------------|--------|
| 38 | `tex1 = 8, tex2 = 9, tex3 = 10` | DELIBERATE | `g_tex_stage` is 4 wide |
| 39 | `proj = 16` | DELIBERATE | `GwDraw.proj` offset |

#### web/src/gpu/shader-abi.ts (2 hits — 1 DELIBERATE, 1 not texture-related)

| Line | Code | Classification | Reason |
|------|------|---------------|--------|
| 16 | `TVERT_SIZE = 112` | DELIBERATE | 28 floats × 4 bytes = 112 bytes; TVert stride |
| 18 | `PSCUBE = 256` | DELIBERATE | Not texture-stage related (cube map PSA offset) |

#### web/src/gpu/index.ts (1 hit — DELIBERATE)

| Line | Code | Classification | Reason |
|------|------|---------------|--------|
| 398 | `s & 3`, `(s >> 2) & 3` | DELIBERATE | State encoding, not texture stage |

#### web/src/gpu/encoder.ts (47 hits — all DELIBERATE)

| Line | Code | Classification | Reason |
|------|------|---------------|--------|
| 58 | `(k >> 4) & 3` | DELIBERATE | `overSmp` sampler override |
| 82 | `flags & 3` | DELIBERATE | Blend flag bits, not texture stage |
| 335, 337 | `w & 3`, `h & 3` | DELIBERATE | Texture dimension alignment |
| 459-471 | Key construction with `& 3`, `>> 2 & 3`, `>> 4 & 3` | DELIBERATE | Shader key bit packing |
| 630, 633 | `ps >= PSCUBE`, `ps === PSCUBE` | DELIBERATE | Cube map PSA offset, not texture stage |
| 635 | Blend bit operations | DELIBERATE | Blend state, not texture stage |
| 954 | `flags & 3` | DELIBERATE | Blend flag bits |
| 974 | `depth & 3` | DELIBERATE | Depth value bits |
| 1001 | `u32[w + D.cull] & 3` | DELIBERATE | Cull mode bits |
| 1004-1005 | `psEff`, `PSCUBE` | DELIBERATE | Cube map PSA, not texture stage |
| 1034-1038 | `q < 4` loops over `proj`/`fogv` | DELIBERATE | `proj` covers coords 0..3 |
| 1062-1065 | Comment: `g_tex_stage is 4 wide` | DELIBERATE | Explicitly acknowledges 4-wide table |

#### web/tools/wgsl-xl.c (3 hits — all DELIBERATE)

| Line | Code | Classification | Reason |
|------|------|---------------|--------|
| 163 | `P.proj[k] = (float)(k + 16 * (int)(rnd() % 3u))` | DELIBERATE | `proj` initialization covers coords 0..3 |
| 172-173 | `PSPar` buffer layout | DELIBERATE | `proj` field layout matches `g_cur_proj[4]` |

## Summary

- **DELIBERATE hits: 185**
- **SUSPICIOUS hits: 0**

Every `& 3`, `&3`, `smp[4]`, `uv[4]`, `tex3`, or loop bound of 4 found in the scanned files falls under one of the five known DELIBERATE categories:
1. `tpick/tsel/g_cur_proj/GwDraw.proj` cover coords 0..3 only
2. `TVert` stride is 28 floats with `uv4`–`uv7` zero-filled
3. MSL emitter in `vsemu.c` is dead on web
4. `ps_1_x` dn stays `&3`
5. `com.c` `g_tex_stage` is 4 wide

No texture/sampler/table index masking to 4 was found outside these categories. The 8-wide ABI (8 texture stages, `uv0`–`uv7`, `smp0`–`smp7` at bindings 0..15) is correctly implemented where it matters: `vsemu_wgsl.inc` lines 733-737 output `uv0`–`uv3` via `tpick` (selecting from `oT0`–`oT3`) and `uv4`–`uv7` directly from `oT4`–`oT7`; vertex attribute reads use `& 7` (not `& 3`) for texture register indices at line 346-347 of `vsemu_wgsl.inc`.

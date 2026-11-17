# The 8-wide inter-stage ABI: frozen design

`debug/shader-differential.md` (Class A) shows the port's inter-stage ABI carries
**four** texture coordinates and **four** samplers, masking every register index
`& 3` / `& 7`.  The ProStreet corpus needs more: 75 of 297 vertex shaders write
`oT4..oT7`, and 34 of 294 pixel shaders use `a4..a7` or `s4+` (max sampler stage
6; `ps_2_0_157` declares `a0..a7`).  That aliasing explains 73 of the 77 failing
vertex modules and 26 of the 78 failing pixel modules in the whole-corpus
differential.  It is inherited from `reference/nfsu2-web`, i.e. not a port
regression, but it is the top remaining coverage gap.

This document **freezes the widened interface** so the C emitter, the TS ABI, the
CPU oracle and the harness can be changed independently and still meet.

## Measured corpus facts that shaped the design

* No failing pixel shader uses `a4..a7` as a **texture coordinate**; the only
  shader in the whole corpus that does is `ps_2_0_157` (8 taps, sampler `s0`).
* `a4..a7` are used as **data** (fed by `i.uv4..i.uv7`), and `s4..s6` as
  **samplers**, far more often.
* `g_smp_raw[16][20]` and `g_tss_raw[8][40]` already hold the game's state for
  all 16 sampler stages and all 8 texture stages; only the *port's* `smp_push`
  loop stops at `st < 4`.
* D3D9 defaults `D3DTSS_TEXCOORDINDEX[st] = st` and projection mode 0.  The
  port's `texproj_push` (com.c) only fills stages 0..3, so for stages 4..7 the
  projection mode is 0 (identity) **by the port's own construction**, not by
  guesswork.

These let the design stay smaller than the obvious one: the vertex-output
selector (`tsel`) and the pixel projection array (`proj`) do **not** need to
widen, and `TVert` keeps its 28-float stride.

## Frozen interface

### 1. `VOut` (the inter-stage struct, every WGSL module)

```wgsl
struct VOut {
  @builtin(position) pos: vec4f,
  @location(0) col0: vec4f,
  @location(1) col1: vec4f,
  @location(2) uv0: vec4f,
  @location(3) uv1: vec4f,
  @location(4) uv2: vec4f,
  @location(5) uv3: vec4f,
  @location(6) uv4: vec4f,
  @location(7) uv5: vec4f,
  @location(8) uv6: vec4f,
  @location(9) uv7: vec4f,
  @location(10) fog: f32,
}
```

11 locations, 41 interpolated components.  `VOUT_LOCATIONS = 11`.

### 2. group 1 bindings

`tex0..tex7` at bindings `0..7`, `smp0..smp7` at bindings `8..15`.  A cube twin
makes binding 0 a `texture_cube<f32>`; every other texture is `texture_2d<f32>`.

### 3. Vertex emitter (`web/runtime/web/vsemu_wgsl.inc`)

* `w_vsdst`, `case R_OUTPUT`: `snprintf(o, cap, "oT%d", n & 7)` (was `& 3`).
* Epilogue declares `var oT0 .. oT7` (was `oT0..oT3`).
* `o.uv0..uv3` keep the existing 4-way `tpick(KH.y >> (8*q) & 3u, oT0, oT1, oT2, oT3)`.
  **`tpick` is unchanged** — `tsel` stays 4 x 8 bits, because D3DTSS_TEXCOORDINDEX
  for stages 0..3 is only ever 0..3 (com.c `texproj_push` clamps `ci > 3 -> st`).
* `o.uv4 = oT4; o.uv5 = oT5; o.uv6 = oT6; o.uv7 = oT7;` — identity, which is the
  D3D9 default for stages 4..7 (the port never conveys a different index).

### 4. Pixel emitter (`web/runtime/web/vsemu_wgsl.inc`)

* `dcl aN`: `k = RN(y->dst) & 7`; `t%d = select(vec4f(0.0), i.uv%d, PS20_TEXCOORD)`.
* `tcuse` mask: `RN(...) & 7` (both the `R_ADDR` and the `nsrc == 0` branches).
* `for k < 8 if (tcuse & (1u << k)) let tc%d = tcoord(i.uv%d, <proj>);` where
  `<proj>` is the literal `P.proj[k]` for `k < 4` and the literal `0.0` for
  `k >= 4` (D3D9 default mode 0 => `tcoord` divides by 1).  This avoids indexing
  a `vec4f` out of range and avoids widening `PSPar`.
* `O_TEX` / `O_TEXLDD` / `O_TEXLDL`: `smp = RN(y->src[1]) & 7`,
  `crd = RN(y->src[0]) & 7`; the `dn = RN(y->dst) & 3` sampler for the ps_1_x
  forms is **unchanged** (ps_1_x has 4 stages).
* `O_TEXCOORD`: `dn = RN(y->dst) & 7`.
* `O_TEXKILL`, `R_ADDR`: `i.uv%d` with `RN(y->dst) & 7`.
* `w_textures`: 8 textures then 8 samplers.

### 5. Pixel oracle (`web/runtime/web/vsemu_psref.inc`, `web/runtime/web/runtime/vsemu.h`)

* `PSRefIn { float col[2][4], uv[8][4], fog; }` (41 floats; was `uv[4][4]`).
* `PSRefPar` unchanged (`proj[4]`).
* `PSRefExec.t[4][4] -> t[8][4]`; `dst_write` / `psr_reg` `R_ADDR` `& 3 -> & 7`.
* `ps_ref_run`: `float tc[8][2]`; `for k < 8`, `m = (k < 4) ? (((int)P->proj[k]) >> 4) : 0`.
* dcl-uv copy `& 3 -> & 7`; `O_TEXCOORD` `& 3 -> & 7`; texld `m = RN(src[0]) & 7`,
  `smp = RN(src[1]) & 7`; `dn` stays `& 3`.

### 6. Reference-vector formats (`web/tools/wgsl-xl.c`)

* `.vec` expected row **29 -> 45**: `pos(4), col0(4), col1(4), uv0..uv7(32), fog(1)`.
  `memcpy(e + 12, o.tex[0], 128)` (8 x vec4), `e[44] = fog`.
* `.pvec` input row **25 -> 41**: `col0(4), col1(4), uv0..uv7(32), fog(1)`.
  `for k < 8` random `uv[k]`, `memcpy(row + 8, in[i].uv, 128)`, `row[40] = fog`.
  The PSPar block (140 floats) and the `np x 5` expected block are **unchanged**.
* `P.proj[k]` stays `for k < 4`.

### 7. Harness (`web/src/test/shadercheck.ts`)

* `vsDiff`: `.vec` expected stride 29 -> 45; `OUTV` count `arrayLength / 45`;
  write `uv4..uv7`; `FIELD` gains `uv4..uv7`; buffer sizes `nv * 45 * 4`.
* `psDiff`: `.pvec` input row 25 -> 41 (`np * 164` bytes at offset 576);
  compute twin loads `i.uv4..i.uv7` from `PIN[b + 24 .. b + 39]`, `i.fog = PIN[b + 40]`.
* `psDiff` entry-point regex must accept **both** emitted forms:
  `@fragment fn (ps_\d+)\(i: VOut\) -> (@location\(0\) vec4f|PSOut) \{`, rewritten to
  `fn psfn(i: VOut) -> <same> {`; the call site is `let c = psfn(i).col;` for the
  `PSOut` form and `let c = psfn(i);` otherwise.  This unblocks the 58 PS modules
  that write `oDepth` (`-> PSOut {`), which the old regex could not match.

### 8. `web/src/gpu/wgsl.ts`

* `VOUT` = the struct above.
* `TVERT_VS`: **keep 28 floats per vertex** (gfx.h `TVert` has `t[4]`); zero-fill
  `o.uv4 = o.uv5 = o.uv6 = o.uv7 = vec4f(0.0)`.
* `FF_FS` unchanged (samples `uv0` only).

### 9. `web/src/gpu/shader-abi.ts`

* `VOUT_LOCATIONS = 11`.
* `textures(cube)`: `for k < 8` texture at binding `k`, then `for k < 8` sampler
  at binding `8 + k`.

### 10. Sampler state for stages 4..7 (`gpu_web.c`, `gpu_proto.h`, `com.c`, `encoder.ts`)

The game already tracks 16 sampler stages (`g_smp_raw[16][20]`); only the port's
hand-off is 4 wide.  Widen it end to end:

* `com.c smp_push`: `unsigned k[8]; for (st = 0; st < 8; st++) ...`.
* `gpu_web.c`: `g_cur_smp[4] -> [8]`; `metal_set_samplers(const unsigned k[8])`;
  `smp_eff(unsigned tex, uint32_t out[8])`; `raw_rec` / `raw_merge` copy 8 words
  into `D->smp`.  `g_cur_proj[4]` and the `tsel` packing are **unchanged**.
* `gpu_proto.h GwDraw`: `uint32_t smp[4] -> smp[8]`, and `#define GW_VERSION 7u`
  -> `8u` (the renderer refuses a control block whose layout word differs).
  Every offset from `blit_src` on shifts by **+16 bytes**; `sizeof(GwDraw)`
  180 -> 196:

  | field | old | new |
  |---|---|---|
  | `proj` | 64 | 64 |
  | `smp` | 80 | 80 |
  | `blit_src` | 96 | 112 |
  | `bcol` | 100 | 116 |
  | `cull` | 104 | 120 |
  | `fogv` | 108 | 124 |
  | `clr_argb` | 124 | 140 |
  | `clr_flags` | 128 | 144 |
  | `clr_z` | 132 | 148 |
  | `ksz` | 136 | 152 |
  | `ix_isz` | 140 | 156 |
  | `ix_ib` | 144 | 160 |
  | `ix_base` | 148 | 164 |
  | `clip` | 152 | 168 |
  | `sizeof` | 180 | 196 |

* `web/src/gpu/proto.ts` (the page side of `gpu_proto.h`): `DRAW_BYTES = 180 ->
  196`, `DRAW_WORDS = 45 -> 49`, `GW_VERSION = 7 -> 8`, and the `D` enum
  (`smp = 20` unchanged; `blitSrc 24 -> 28`, `bcol 25 -> 29`, `cull 26 -> 30`,
  `fogv 27 -> 31`, `clrArgb 31 -> 35`, `clrFlags 32 -> 36`, `clrZ 33 -> 37`,
  `ksz 34 -> 38`, `ixIsz 35 -> 39`, `ixIb 36 -> 40`, `ixBase 37 -> 41`,
  `clip 38 -> 42`, `viewport 39 -> 43`).  `LAYOUT` follows from `DRAW_BYTES`.
* `web/tools/check-gpu-proto.mjs` (new, committed with this doc) compares the
  two and is gate 6 below.
* `encoder.ts`: the `D` offset table, `DRAW_WORDS`, and the group-1 bind group
  (8 textures + 8 samplers, reading `D.smp[0..7]`).
* `null_platform.c`: the weak `metal_set_texproj`/`metal_set_samplers` stubs.

## Explicitly NOT changed (and why)

* **`tpick`** — 4-way, because `tsel` stays 4 x 8 bits (see 3).
* **`tsel` / `g_cur_proj` / `GwDraw.proj`** — coordinates 0..3 only (see 3).
* **`PSPar` / `PSRefPar.proj`** — 4 wide; stages 4..7 use mode 0 (see 4, 5).
* **`TVert` / `TVERT_SIZE` (112) / the 28-float stride** — gfx.h `TVert` carries
  `t[4]`; the CPU vertex path zero-fills `uv4..uv7`.
* **MSL emitter (`vsemu.c`)** — the Mac path is dead on web; the 8-wide widening
  is scoped to the WGSL emitter + CPU oracle + harness.  (Its 4 live native gaps
  D2/D3/D7 are tracked separately.)
* **`analysis/webgpu-test/draw_real.html`** — it embeds a verbatim emitted module
  and builds its own layout, so it is self-consistent, but its claim that the
  embedded `WGSL_PS` is byte-identical to `shader2wgsl ps_2_0_042.bin` becomes
  false once the emitter changes: **regenerate it**.

## Implementation halves (disjoint file sets)

**Half A — GPU / ABI / emitter.**  `web/runtime/web/vsemu_wgsl.inc`,
`web/src/gpu/wgsl.ts`, `web/src/gpu/shader-abi.ts`, `web/src/gpu/proto.ts`,
`web/src/gpu/encoder.ts`, `web/runtime/web/gpu_web.c`,
`web/runtime/web/gpu_proto.h`, `web/runtime/web/runtime/com.c`,
`web/runtime/web/null_platform.c`.

**Half B — oracle / harness / vectors.**  `web/runtime/web/runtime/vsemu.h`,
`web/runtime/web/vsemu_psref.inc`, `web/tools/wgsl-xl.c`,
`web/src/test/shadercheck.ts`.

The two halves meet only through this document.

## Verification gates (all must pass before merge)

1. `bash lifter/tools/shader_vec_diff.sh` — `ok` must go from `false` to `true`,
   or at worst the only remaining failures are the Class B/C/D tail
   (no `oT4..oT7` / `a4..a7` / `s4+` aliasing anywhere).
2. `gcc -O2 -Wall -Wextra -DVSEMU_WGSL ...` of `wgsl-xl.c` + `vsemu.c` is clean.
3. `J=2 FULL=1 web/build.sh` exits 0 (`out/web/nfsu2.wasm` still ~57 MB).
4. `naga` validates all 591 emitted modules; `lifter/tools/corpus_check.sh`
   still reports SHADERCHECK PASS.
5. `lifter/tools/web/vsemu_wgsl.inc` refreshed byte-identical to
   `web/runtime/web/vsemu_wgsl.inc`.
6. `node web/tools/check-gpu-proto.mjs` exits 0 (the C header, `src/gpu/proto.ts`
   and `GW_VERSION` agree).

## Staging

Do **Half B first**: it is pure test/tooling (the game does not link
`vsemu_psref.inc` or `wgsl-xl.c`), so it cannot destabilise the runtime, and it
makes the differential able to *see* the ABI classes.  Then Half A, gated on 1-6.

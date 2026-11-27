# ProStreet shader emitter semantic audit

## Scope

Read-only audit of the WGSL emitter switches in `web/runtime/web/vsemu_wgsl.inc` against D3D9 shader assembly semantics, for the opcodes actually present in the 591-blob corpus at `/tmp/opencode/ps-shaders/`. No runtime files were modified.

## Opcode presence in corpus

From the token walker (`/tmp/opencode/opcode_walker.py`):

| Opcode | Name | ps_2_0 | ps_3_0 | vs_1_1 | vs_2_0 | vs_3_0 |
|--------|------|--------|--------|--------|--------|--------|
| 1 | O_MOV | 491 | 500 | 104 | 461 | 542 |
| 2 | O_ADD | 264 | 1096 | 59 | 152 | 303 |
| 4 | O_MAD | 242 | 1239 | 84 | 378 | 450 |
| 5 | O_MUL | 460 | 1327 | 50 | 214 | 564 |
| 6 | O_RCP | 316 | 380 | 13 | 19 | 69 |
| 7 | O_RSQ | 12 | 76 | 9 | 9 | 37 |
| 8 | O_DP3 | 72 | 330 | 26 | 93 | 201 |
| 9 | O_DP4 | 7 | 79 | 114 | 1153 | 680 |
| 10 | O_MIN | 23 | 66 | 11 | 73 | 49 |
| 11 | O_MAX | 23 | 130 | 9 | 65 | 33 |
| 12 | O_SLT | — | — | 8 | 42 | 61 |
| 13 | O_SGE | — | — | — | 4 | 14 |
| 14 | O_EXP | 12 | 6 | — | 21 | 88 |
| 15 | O_LOG | 12 | 6 | — | 11 | 60 |
| 16 | O_LIT | — | — | 6 | 9 | — |
| 18 | O_LRP | 11 | 40 | — | 11 | 13 |
| 19 | O_FRC | 1 | — | 6 | 30 | 40 |
| 31 | O_DCL | 497 | 681 | 78 | 401 | 884 |
| 32 | O_POW | 20 | 78 | — | 19 | 50 |
| 35 | O_ABS | 8 | 2 | — | — | — |
| 36 | O_NRM | 25 | 153 | — | 12 | 17 |
| 37 | O_SINCOS | — | — | — | 1 | 5 |
| 38 | O_REP | — | 3 | — | — | — |
| 39 | O_ENDREP | — | 3 | — | — | — |
| 46 | O_MOVA | — | — | — | 23 | 14 |
| 48 | O_DEFI | — | 3 | — | — | — |
| 65 | O_TEXKILL | 38 | — | — | — | — |
| 66 | O_TEX | 234 | 499 | — | — | — |
| 81 | O_DEF | 164 | 313 | 19 | 226 | 179 |
| 88 | O_CMP | 10 | 177 | — | — | — |
| 90 | O_DP2ADD | 6 | 57 | — | — | — |
| 91 | O_DSX | — | 11 | — | — | — |
| 92 | O_DSY | — | 7 | — | — | — |

Legend: — = not present in that class.

## Emitter reference

The two switches in `web/runtime/web/vsemu_wgsl.inc`:

- **Pixel shader switch** (line ~359): `ps_to_msl_ex2()` path. Operands `s0`=`src[0]`, `s1`=`src[1]`, `s2`=`src[2]` are set up at line ~354 before the switch.
- **Vertex shader switch** (line ~589): `vs_to_msl()` path. Same operand convention.

WGSL `select(accept, reject, condition)`: if condition component is non-zero → accept, else → reject.

## Per-opcode semantic audit

### O_CMP (88) — pixel only — OK

**D3D9 semantics:** `cmp dst, src0, src1, src2`: per component, if src0 >= 0 then dst = src1, else dst = src2. (Microsoft: https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/cmp---ps)

**Emitter:** line 421: `select((%s), (%s), (%s) >= vec4f(0.0))` with args `(s2, s1, s0)` → WGSL `select(accept, reject, condition)` = `select(src2, src1, src0 >= 0)`.

**Verdict: OK** — confirmed against Microsoft's D3D9 cmp spec. The emitter is correct.

### O_SGE (13) — vertex only — OK

**D3D9 semantics:** `sge dst, src0, src1`: if src0 >= src1 then dst = 1 else dst = 0.

**Emitter:** `select(vec4f(0.0), vec4f(1.0), (%s) >= (%s))` with `(s0, s1)` → if s0 >= s1 then 1 else 0.

**Verdict: OK**

### O_SLT (12) — vertex only — OK

**D3D9 semantics:** `slt dst, src0, src1`: if src0 < src1 then dst = 1 else dst = 0.

**Emitter:** `select(vec4f(0.0), vec4f(1.0), (%s) < (%s))` with `(s0, s1)` → if s0 < s1 then 1 else 0.

**Verdict: OK**

### O_LRP (18) — pixel and vertex — OK

**D3D9 semantics:** `lrp dst, src0, src1, src2`: dst = src0 * (src1 - src2) + src2. (Microsoft: https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/lrp---vs)

**Emitter:** lines 411 (pixel) and 603 (vertex): `mix((%s), (%s), (%s))` with args `(s2, s1, s0)` → WGSL `mix(src2, src1, src0)` = src2*(1-src0) + src1*src0 = src2 + src0*(src1 - src2).

**Verdict: OK** — confirmed against Microsoft's D3D9 lrp spec. Since mix(a,b,t) = a*(1-t)+b*t, mix(s2, s1, s0) = src2*(1-src0) + src1*src0 = src2 + src0*(src1 - src2) = D3D9 lrp. The emitter is correct.

### O_FRC (19) — pixel and vertex — OK

**D3D9 semantics:** `frc dst, src`: dst = fractional part of src (per component).

**Emitter:** `fract(%s)` with `s0` → `fract(src0)`.

**Verdict: OK**

### O_SINCOS (37) — vertex only — OK

**D3D9 semantics:** `sincos dst, src`: dst = (cos(src.x), sin(src.x), 0, 0).

**Emitter:** `vec4f(cos((%s).x), sin((%s).x), 0.0, 0.0)` with `s0`.

**Verdict: OK**

### O_LIT (16) — vertex only — OK

**D3D9 semantics:** `lit dst, src`: dst.x = 1; dst.y = max(src.x, 0); dst.z = (src.x > 0) ? pow(max(src.y, 0), clamp(src.z, -127.9, 127.9)) : 0; dst.w = 1.

**Emitter:** `vec4f(1.0, max((%s).x, 0.0), select(0.0, pow(max((%s).y, 0.0), clamp((%s).w, -127.9, 127.9)), (%s).x > 0.0), 1.0)` with `s0`.

**Verdict: OK** — matches D3D9 lit semantics exactly.

### O_DP2ADD (90) — pixel only — OK

**D3D9 semantics:** `dp2add dst, src0, src1, src2`: dst = dot(src0.xy, src1.xy) + src2 (src2.x used as scalar, broadcast to all components).

**Emitter:** `vec4f(dot((%s).xy, (%s).xy) + (%s).x)` with `(s0, s1, s2)` → dot(s0.xy, s1.xy) + s2.x, replicated to all 4 components.

**Verdict: OK**

### O_MOVA (46) — vertex only — OK

**D3D9 semantics:** `mov.a dst, src`: move with address register (relative addressing). Without relative addressing, equivalent to MOV.

**Emitter:** `%s` with `s0` → copies src directly. Relative addressing is handled separately via the `rel[]` mechanism in operand decoding.

**Verdict: OK** — for non-relative cases, MOVA == MOV.

### O_TEXKILL (65) — pixel only — OK

**D3D9 semantics:** `texkill dst`: discard if any component of the texture coordinate register < 0. For ps_2.0+, tests all 4 components.

**Emitter:** `if (any(%s < vec4f(0.0))) { discard; }` for ps_2.0+ (version >= 0x0200).

**Verdict: OK**

### O_TEX (66) — pixel only — PARTIAL MISMATCH (projective)

**D3D9 semantics:** `texld dst, src, sampler`: sample texture. If the source coordinate register has a projective W component (D3DTSS_TEXCOORDINDEX with projective flag), the hardware divides xyz by w before sampling.

**Emitter:** 
- Non-projective (src is a normal register): `textureSample(texN, smpN, tcN)` — passes coordinate directly.
- Projective (src is R_ADDR, dependent read): `textureSample(texN, smpN, i.uvN.xyz)` or `tcN`.

**Mismatch:** WGSL's `textureSample` does **not** perform automatic projective divide (xyz/w). D3D9 hardware does. The emitter passes the coordinate register directly without dividing by w. For shaders using projective texturing, the emitted WGSL will sample at un-divided coordinates, producing incorrect results.

**Note:** No shader in the corpus was verified to actually use projective texturing, so this is a latent issue, not an observed failure.

**Verdict: PARTIAL MISMATCH** — projective texturing lacks xyz/w divide in WGSL emission.

### O_DSX (91) / O_DSY (92) — pixel only (ps_3_0) — OK

**D3D9 semantics:** `dsx dst, src`: partial derivative of src with respect to screen X. `dsy dst, src`: partial derivative with respect to screen Y.

**Emitter:** `dpdx(%s)` / `dpdy(%s)` with `s0` → WGSL `dpdx()` / `dpdy()`.

**Verdict: OK**

## Summary of mismatches

| Opcode | Name | Class | D3D9 rule | Emitter output | Verdict |
|--------|------|-------|-----------|----------------|---------|
| 66 | O_TEX | ps | projective divide xyz/w | no divide | **PARTIAL** — latent, depends on shader usage |

All other audited opcodes (O_CMP, O_LRP, O_SGE, O_SLT, O_FRC, O_SINCOS, O_LIT, O_DP2ADD, O_MOVA, O_TEXKILL, O_DSX, O_DSY) match D3D9 semantics.

## RESOLVED: CPU vs_run interpreter CMP and LRP

**File:** `web/runtime/web/runtime/vsemu.c`.

The CPU interpreter used to have no `O_CMP` case (so `cmp` left the destination
untouched) and computed `lrp` as `s1 + s0*(s2-s1)`, swapping src1/src2 versus
D3D9. It now uses the D3D9 formulas:

```c
case O_CMP: for(int i=0;i<4;i++) d[i]= s[0][i]>=0.f ? s[1][i] : s[2][i];
            DSTW(x,d); break;
case O_LRP: for(int i=0;i<4;i++) d[i]=s[2][i]+s[0][i]*(s[1][i]-s[2][i]);
            DSTW(x,d); break;
```

**Verification:** `bash lifter/tools/cpu_shader_semantics_test.sh` builds a
synthetic vs_2_0 shader with c0 = (-1,1,-0.5,2), c1 = (11,12,13,14),
c2 = (21,22,23,24), c3 = (+0,-0,NaN,3) and checks
`oT0(cmp) = (21,12,23,14)`, `oT1(lrp) = (31,12,28,4)`,
`oT2(edges) = (11,12,23,14)` and `oT3(mask) = (21,0,0,0)`. The Node hook
`web/test/cpu-semantics.test.mjs` also compiles deliberately broken copies:
removing `O_CMP` makes `oT0` zero, and swapping the `lrp` operands gives
`(1,22,8,34)`. This is CPU-semantics-only coverage, not a corpus or GPU
differential.

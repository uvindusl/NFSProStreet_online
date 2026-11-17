# ProStreet vertex path differential audit

Status: **corrected**. An earlier draft of this file (pH, w1F:pH) claimed O_SLT and
O_SGE were inverted and that O_LOG wrote four different values in the CPU but one
in WGSL. Both claims are wrong; the corrections and the evidence are below. Every
line number was re-read from the source and every semantic was checked against the
Microsoft D3D9 vertex-shader instruction reference.

## Method

Three vertex implementations exist and were compared:

| Path | File | Role |
|---|---|---|
| `vs_run` (CPU) | `web/runtime/web/runtime/vsemu.c` (~line 450-560) | CPU interpreter; runs a shader the WGSL emitter rejects |
| `vs_to_wgsl` (WGSL) | `web/runtime/web/vsemu_wgsl.inc` (607-668) | browser GPU emitter |
| `vs_to_msl` (MSL) | `web/runtime/web/runtime/vsemu.c` (1340-1406) | Mac reference; the WGSL emitter is a port of it |

For every opcode handled by `vs_to_wgsl` the emitted text was compared, character
for character, against `vs_to_msl`. **The WGSL emitter is a faithful port of the
MSL emitter**: every pattern is identical modulo `float4` -> `vec4f`. The port
introduced no new divergence. What follows are therefore CPU-vs-GPU differences,
and the question is which side matches D3D9.

Ground truth: the D3D9 vs instruction pages on learn.microsoft.com (crs, nrm,
logp, expp, m4x3, m3x2).

## Corrections to the earlier draft

1. **O_SLT / O_SGE are MATCHES, not mismatches.** The draft read WGSL
   `select(a, b, cond)` as returning `a` when `cond` is true. WGSL
   `select(f, t, cond)` returns **`t` when `cond` is true**. So
   `select(vec4f(0.0), vec4f(1.0), (s0) < (s1))` = `s0<s1 ? 1 : 0`, identical to
   the CPU at vsemu.c:463 and to D3D9. The same holds for O_SGE (vsemu.c:464,
   vsemu_wgsl.inc:618). Cross-check inside the same file: the four-way channel
   select at vsemu_wgsl.inc:97 only makes sense with `select(f, t, cond)`.

2. **O_LOG is a MATCH.** The CPU at vsemu.c:487-489 broadcasts one value
   (`d[0]=d[1]=d[2]=d[3]=r`), exactly like the WGSL at vsemu_wgsl.inc:630. The
   four-different-values form belongs to **O_LOGP** (vsemu.c:499-503), not O_LOG.
   The draft conflated the two.

3. **O_M3x2 emits `(dot0, dot1, 0.0, 1.0)`, not `(dot0, dot1, 1.0, 0.0)`.** The
   fill loop is `for (rr = rows; rr < 4; rr++) ... rr == 3 ? "1.0" : "0.0"`
   (vsemu_wgsl.inc:655). So d[2]=0.0 (matches the CPU) and only d[3] differs. The
   draft reported d[2]=1.0.

## Per-opcode comparison

| Opcode | D3D9 semantics | CPU `vs_run` | WGSL `vs_to_wgsl` | Verdict |
|---|---|---|---|---|
| O_MOV | dst = src0 | `d=s0` | `s0` | MATCH |
| O_MOVA | dst = src0 | `d=s0` | `s0` (int rounding spelled out) | MATCH |
| O_ADD/SUB/MUL/MAD | componentwise | same | same | MATCH |
| O_MIN/MAX | componentwise | same | `min`/`max` | MATCH |
| O_SLT | (s0<s1)?1:0 | `s0<s1?1:0` | `select(0,1,s0<s1)` = `s0<s1?1:0` | **MATCH** |
| O_SGE | (s0>=s1)?1:0 | `s0>=s1?1:0` | `select(0,1,s0>=s1)` | **MATCH** |
| O_DP3/DP4 | dot, broadcast | same | `vec4f(dot(...))` | MATCH |
| O_LRP | s2+s0*(s1-s2) | same | `mix(s2,s1,s0)` | MATCH |
| O_FRC/ABS/SGN | componentwise | same | `fract`/`abs`/`sign` | MATCH |
| O_RCP/RSQ | 1/x, 1/sqrt(|x|) broadcast | same | same (x==0 -> FLT_MAX) | MATCH |
| O_POW | |x|^y broadcast | same | same | MATCH |
| O_EXP | 2^x broadcast | same | `exp2` | MATCH |
| O_EXPP | (2^floor(x), frac, 2^x, 1) | same | same | MATCH (see note) |
| O_LOG | log2(|x|) broadcast | broadcast | broadcast | MATCH |
| O_LOGP | log2(|x|) broadcast | **4 expp-style values** | broadcast | **CPU wrong** |
| O_SINCOS | (cos, sin, 0, 0) | same | same | MATCH |
| O_LIT | (1, max(x,0), ..., 1) | clamp ±128 | clamp ±127.9 | MATCH* |
| O_DST | (1, y*y, z, w) | same | same | MATCH |
| O_CRS | cross, w masked | d[3]=0 | w=1.0 | masked, moot |
| O_NRM | xyz/|xyz|, w = src0.w/|xyz| | w=src0.w/|s0| | w=src0.w/|s0| | **FIXED** (see below) |
| O_M4x4 | 4 dots | same | same | MATCH |
| O_M4x3 | 3 dots, mask .xyz | d[3]=0 | d[3]=1.0 | masked, moot |
| O_M3x4 | 4 dots of xyz | same | same | MATCH |
| O_M3x3 | 3 dots, mask .xyz | d[3]=0 | d[3]=1.0 | masked, moot |
| O_M3x2 | 2 dots, mask .xy | d[2]=d[3]=0 | d[2]=0, d[3]=1.0 | masked, moot |

*O_LIT: CPU clamps the power to [-128,128] (vsemu.c:507); GPU clamps to
[-127.9,127.9] (vsemu_wgsl.inc:662, vsemu_msl:1403). Negligible.

### Why the O_CRS / O_M4x3 / O_M3x3 / O_M3x2 rows are moot

D3D9 requires restricted destination masks for these instructions - `crs`:
`.x|.y|.z|.xy|.xz|.yz|.xyz` (never `.w`); `m4x3`/`m3x3`: `.xyz`; `m3x2`: `.xy`.
The emitter writes the vec4 and then applies the instruction's destination mask
(`w_vsdst`, vsemu_wgsl.inc:679), and the CPU does the same (`DSTW`). The
component the GPU fills with 1.0 (or the CPU leaves 0) is outside the legal mask,
so it is never written on either path.

### O_LOGP - the CPU fallback is wrong

D3D9 `logp`: `dest.x = dest.y = dest.z = dest.w = log2(abs(src))` (broadcast).
The CPU at vsemu.c:499-503 instead writes `d[0]=floor(log2 a)`, `d[1]=a/2^floor`,
`d[2]=log2 a`, `d[3]=1` - the **expp** shape. So the CPU fallback disagrees with
D3D9 and with the WGSL/MSL emitters. This is dead for the current corpus (see
impact) but is a real latent bug in `vs_run`.

### O_NRM - the live divergence (FIXED)

D3D9 `nrm`: `dest.w = src0.w * (1 / sqrt(src0.x^2+src0.y^2+src0.z^2))`, and the
scale factor is `FLT_MAX` when the sum of squares is zero.
- CPU (vsemu.c:479-482): was `d[3] = 1/|s0.xyz|` (assumed `src0.w == 1`).
- WGSL/MSL (vsemu_wgsl.inc:633, vsemu.c:1370): was `d[3] = 1.0`.

Neither was correct when `src0.w != 1`. `nrm` writes all four components, so unlike
the matrix ops this was not maskable.

**Fixed in `a945de9`** (Claude, w1F:pC) in all three paths: `vs_run`, `vs_to_msl`
and `vs_to_wgsl`, each now `src0.w * f` with `f = (len==0) ? FLT_MAX : 1/len`.
The mirror `lifter/tools/web/vsemu_wgsl.inc` is byte-identical. Regression:
`lifter/tools/nrm_w_test.sh` OK (29 NRM emissions carry the src0.w factor);
`shader2wgsl_test.sh` 591/591; `scripts/validate_wgsl.sh` emitted=591 emitfail=0
naga_fail=0.

## Corpus impact (297 vertex shaders in /tmp/opencode/ps-shaders)

Instruction counts, from `lifter/tools/build/shader_disasm` over every `vs_*.bin`:

| Opcode | Count | Divergence live? |
|---|---|---|
| slt | 111 | no - both paths correct |
| sge | 18 | no - both paths correct |
| nrm | 29 | **yes, for 2 shaders** |
| lit | 15 | negligible (clamp bound) |
| crs, logp, expp, m4x3, m3x3, m3x2, m4x4, m3x4 | 0 | dead code for this corpus |

`nrm` is written as `rX.xyz` in 27 of 29 cases, so `w` is masked and the
divergence is moot. Two shaders write all four:

```
vs_2_0_134.bin: 128 mov r0.w, c120.xxxx
                129 nrm r1, r0
                130 dp4 r0.x, r1, r2     <- reads r1.w
vs_3_0_051.bin: 133 mov r0.w, c120.xxxx
                134 nrm r1, r0
                135 dp4 r0.x, r1, r2     <- reads r1.w
```

`r1.w` is read immediately by the following `dp4`, and the WGSL emitter accepts
both shaders (`out/tools/shader2wgsl ... exit 0`, two `normalize` sites each), so
they run on the GPU path with `r1.w = 1.0`. The D3D9 value would be
`c120.x / |r0.xyz|`. Whether this changes the rendered result depends on `c120.x`
and `|r0.xyz|`; it is a genuine open divergence, not a masked one.

## Opcodes not handled by the vertex emitter

`vs_to_wgsl`'s `default:` (vsemu_wgsl.inc:665-667) rejects the whole shader, which
then runs on the CPU. Valid vertex opcodes that are not handled: O_CND, O_CMP,
O_DP2ADD, O_DSX, O_DSY, O_TEX/O_TEXKILL/O_TEXLDD/O_TEXLDL, O_SETP, O_BREAKP, and
the flow-control ops. These are handled in the pixel switch only.

## Summary

- The WGSL vertex emitter is a faithful port of the MSL emitter; the port added no
  divergence.
- Of the 30 opcodes it handles, 24 match the CPU exactly, 5 differ only outside
  the instruction's legal destination mask (moot), and 1 (O_LOGP) is a latent CPU
  fallback bug that the corpus never reaches.
- O_NRM's `w` component was the one divergence live in the corpus
  (vs_2_0_134, vs_3_0_051); the D3D9-correct value `src0.w * 1/|xyz|` was applied to
  all three paths in `a945de9`, and the corpus now matches D3D9.

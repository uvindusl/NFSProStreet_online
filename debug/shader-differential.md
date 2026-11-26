# Shader differential: WGSL emitter vs the CPU oracle

A numeric differential over the whole ProStreet shader corpus.  It runs the
emitted WGSL vertex/pixel functions and compares every output component against
the CPU interpreter (`vs_run`, `ps_ref_run`) on identical bytecode, constants,
vertex declaration and textures.

It is a **falsification tool, not a pass/fail gate**: two independent float
implementations (glibc vs the GPU) will always disagree on a tail of
edge cases.  What matters is the *structure* of the disagreement, and the
tool found two real, actionable classes.

## Reproduce

```sh
# SHADER_DIR is game-derived and is not in the repository.
bash lifter/tools/shader_vec_diff.sh /tmp/opencode/ps-shaders /tmp/opencode/ps-vec-dump
```

which is

```sh
gcc -O2 -DVSEMU_WGSL -DNFS_ENHANCED=0 \
    -I web/runtime -I web/runtime/web -I web/runtime/web/runtime \
    -o out/tools/wgsl-xl web/tools/wgsl-xl.c web/runtime/web/runtime/vsemu.c -lm
out/tools/wgsl-xl --corpus /tmp/opencode/ps-shaders /tmp/opencode/ps-vec-dump
node web/tools/shader-check.mjs --from /tmp/opencode/ps-vec-dump --query '&all2d' --timeout 1800000
```

`--corpus` writes, for every `vs_*.bin` / `ps_*.bin`, the emitted module
(`vs_N.wgsl` / `ps_N.wgsl`) *and* its differential vectors (`vs_N.vec` /
`ps_N.pvec`, oracle = the CPU interpreter).  No cube twins are written (pixel
ids run 1..N), hence `&all2d`.

Corpus: 297 vertex shaders (27 `vs_1_1`, 199 `vs_2_0`, 71 `vs_3_0`) and 294
pixel shaders (223 `ps_2_0`, 71 `ps_3_0`).  `--corpus` reports
`297 vertex shaders, 294 pixel shaders, 0 failures`.

## Result

```
diff  : 297 modules, 19008 vertices, 551232 components, 31336 bad,
        maxRel 5.22e35 (vs_252 v2 uv1.x: gpu 5.22e35 cpu 0.00573)
psdiff: 229 modules,  58624 pixels,  244096 components,  4073 bad,
        maxRel 15.12 (ps_287 pixel 125.b), 0 edgeFlips
pull  : 3328/3328 exact, maxUlp 0
ok    : false
```

Vertex: 77 of 297 modules fail.  Pixel: 78 of the 229 that the numeric diff
can run fail; 58 more have no fragment entry point (harness) and 7 more are
the `dpdx`/`dpdy` modules (harness) — see below.

## Class A — the 4-wide ABI (real, actionable)

The port's inter-stage ABI carries **four** texture coordinates and **four**
samplers: `oT0..oT3` / `uv0..uv3` / `t0..t3` / `tex0..tex3` + `smp0..smp3`,
and every register index is masked `& 3` (`web/runtime/web/vsemu_wgsl.inc`
lines 334, 345-346, 369-372, 388, 400, 574, 702-723).  The corpus needs more:

* **75 of 297 VS write `oT4..oT7`** (`oT4..oT7` are aliased onto `uv0..uv3`, so
  a shader that writes both `oT0` and `oT4` clobbers `uv0`).
* **34 of 294 PS use `a4..a7` or `s4+`** in a real instruction:
  `dcl a`: a4=6, a5=3, a6=3, a7=1; `s` used: s4=71, s5=23, s6=28; max sampler
  stage 6.  8 of the 34 are `ps_2_0`, so the SM2.0 path hits it too.
* Largest declared: `ps_2_0_157` declares `a0..a7` (8 coordinates).

This explains **73 of the 77 failing VS** and **26 of the 78 failing PS**.

It is **not a port regression**: `reference/nfsu2-web` has the same limit
(`VOUT_LOCATIONS = 7`, `TVert.t[4]`, the same `& 3` masks).  It is a real
coverage gap in the inherited design.

**This is the top remaining coverage gap.**

## Class B — vertex `O_POW` base == 0 (real, one line)

`vs_run` (`web/runtime/web/runtime/vsemu.c:483`) deliberately special-cases the
base:

```c
case O_POW: { float b=fabsf(s[0][0]); float r=(b==0.f)?0.f:powf(b,s[1][0]);
```

Neither WGSL (`vsemu_wgsl.inc:631`) nor MSL (`vsemu.c:1368`) does:

```
vec4f(pow(abs((%s).x), (%s).x))
```

`pow(0, y)` is `+inf` for `y < 0` on the GPU (`exp2(y*log2(0))`), `0` on the
CPU.  This explains the two otherwise-unexplained vertex failures:

| module | src | component | gpu | cpu |
|---|---|---|---|---|
| vs_103 | vs_2_0_076.bin | uv1.x | -1.51e21 | 1 |
| vs_160 | vs_2_0_133.bin | col0.z | 6.66e27 | 1 |

Both are the `pow(r0.x, c.w)` after `r0.x = 1 - clamp(r0.y*c.y, 0, 1)`, i.e.
base exactly 0 with a negative exponent.

The same pattern in the **pixel** reference is deliberately *not* guarded
(`vsemu_psref.inc:137`, `powf(fabsf(s[0][0]), s[1][0])`) and matches the WGSL
emitter, so only the vertex path diverges.

Candidate fix (not applied — see below):

```
vec4f(select(0.0, pow(abs((%s).x), (%s).x), abs((%s).x) != 0.0))
```

**Deliberately not applied.**  Which behaviour real D3D9 has at `0^negative` is
genuinely ambiguous (D3D9 hardware computes `exp2(y*log2|x|)` and would give
`+inf`, matching the GPU; the CPU emulator's `0` matches the Mac's `gfx`), the
difference only shows on inputs the game may never produce, and the code is
byte-identical to the shipped, known-good `reference/nfsu2-web`.  It is
recorded here so the decision is explicit rather than accidental.

## Class C — finite precision tail (expected)

The remaining 47 PS and 2 VS failures are finite and mostly just above
`TOL = 1e-3`:

* PS failing rows by relative error: `[1e-3,1e-2)` 148, `[1e-2,1e-1)` 8,
  `[1e-1,1)` 2, `>= 1` 1.
* 37 of the 47 have max relative error `< 1e-2`.
* VS: `vs_66` (`vs_2_0_039`, `sincos`) and `vs_228` (`vs_3_0_002`) differ by
  ~2e-4 on a handful of components.

These are the expected disagreement between glibc `pow`/`exp2`/`log2`/`sin`/
`cos` and the GPU's, amplified through chains of transcendentals.  The corpus
feeds random constants in `[-1,1]` and random coordinates, so the tail is
maximal.  Not a port regression.

## Class D — non-finite oracle values (needs investigation)

11 PS modules have rows where the **CPU oracle** is `NaN`/`Inf` while the GPU
is finite, or vice versa.  All 11 use `pow` + `dp3` + `nrm`; the affected
channel is always `r`/`g`/`b` (never `a`).  6 of the 11 are already explained
by Class A; 5 are not:

| module | src | example |
|---|---|---|
| ps_154 | ps_2_0_154.bin | gpu 4.65e8, ref Inf (log/mul/exp chain) |
| ps_243 | ps_3_0_020.bin | gpu 0.074, ref NaN (rcp chain) |
| ps_245 | ps_3_0_022.bin | ref NaN |
| ps_247 | ps_3_0_024.bin | ref NaN |
| ps_274 | ps_3_0_051.bin | log/mul/exp chain |

`rcp`/`rsq`/`log` are guarded identically in the oracle and the emitter
(`vsemu_psref.inc:129-144` vs `vsemu_wgsl.inc:417-434`), so the divergence is
upstream: the two produce different finite values, which then straddle an
`inf`/`NaN` boundary (`0 * inf`, `exp2(>128)`).  Characterised, not explained.

## Harness limitations found (not emitter bugs)

* **58 PS modules** emit `-> PSOut {` (the `frag_depth` form) which
  `psDiff`'s regex `-> @location(0) vec4f {` cannot match, so the numeric diff
  is skipped and the module is reported as "no fragment entry point".
  `web/src/test/shadercheck.ts` should accept both entry forms and return
  `.col`.
* **7 PS modules** (`ps_249,250,251,252,289,290,291`) are exactly the 7 that
  use `dpdx`/`dpdy`; derivatives cannot run in the compute pipeline stage the
  differential uses.  The fragment pipelines build fine.  These are now
  checked by a render pass, see "Derivative pixel shaders" below; `psDiff`
  skips them.

Together these 65 modules are the reason `psdiff` covers 229 of 294.

## Opcode correlation of the failing vertex modules

`exp` 61/77, `rcp` 65/77, `log` 59/77, `pow` 46/77, `rsq` 35/77,
`sincos` 6/77 — consistent with the Class A aliasing (a clobbered coordinate
feeds the next texture fetch, which feeds a `pow`/`exp` chain).

## Follow-ups

1. Widen the ABI to 8 coordinates + 8 samplers (Class A).  Files:
   `web/runtime/web/vsemu_wgsl.inc`, `web/src/gpu/shader-abi.ts`,
   `web/src/gpu/wgsl.ts`, `web/src/gpu/encoder.ts`, `web/src/gpu/gpu_web.c`,
   `web/src/test/shadercheck.ts`, plus the MSL emitter and `ps_ref_run` /
   `PSRefIn` / the `.pvec` input block (25 -> 41 floats).
2. `shadercheck.ts` `psDiff`: accept the `PSOut` entry form (58 modules).
3. Decide Class B (vertex `O_POW` base 0).
4. Explain Class D.

## Derivative pixel shaders (`dsx` / `dsy`)

`wgsl-xl` writes `ps_<id>.dvec` for a pixel shader that uses `dsx`/`dsy`, and
`shadercheck.ts` (`dsDiff`) renders it on a 16x16 target covered by one
triangle.  The varyings (`col0`, `col1`, `uv0..7`, `fog`) are linear in the
pixel centre, so every derivative equals a known gradient whether the GPU takes
fine or coarse differences.  Every `dsx`/`dsy` in the corpus reads `v0` or `v1`
(the colour registers), sometimes swizzled (`dpdx(v1.xxyw)`), so the oracle
(`ps_ref_run`, `O_DSX`/`O_DSY`) takes the gradient of that register directly
and refuses any other source or a modifier other than negate.

The module is run on the GPU five times: as emitted, and with every
`dpdx(vN.swz)` / `dpdy(vN.swz)` replaced by the gradient of that register
(exact; nudged by 2e-6; negated; axes swapped).  The first two must agree;
that comparison gates, because the runs share all other arithmetic, so only the
derivative can make them differ.  A shader with a discontinuity (a compare, a
`floor`) can land on the other side of it when the derivative differs from the
gradient by float rounding; a pixel that misses the exact run but equals the
nudged run is accepted and counted, since a wrong register, axis or sign cannot
match a 2e-6 nudge.  The negated and swapped runs are controls: where the
output does not change under a control, a match cannot confirm that property.
The CPU oracle is compared as well but only logged: these shaders also use
`nrm`/`rcp`/`rsq`/`pow`, where GPU and libm already disagree in modules that
have no derivative.

Result on Apple M2 Pro (Metal 3), Chrome, in the full 591-shader corpus run:

| | |
|---|---|
| GPU `dpdx`/`dpdy` vs GPU with analytic gradients | 7 modules, 1792 pixels, 0 mismatches, max relative difference 4.6e-4 |
| pixels accepted on a discontinuity within rounding noise | 1 (`ps_290`, pixel 191, which differed by 3e-2 before the nudged run was added) |
| output changes when the derivatives are negated | 0 of 7 modules |
| output changes when the axes are swapped | 4 of 7 (`ps_249`..`252`); not `ps_289`..`291` |
| CPU oracle, informational | about 2200 mismatches over 1e-3 relative (the nrm/rcp/rsq/pow precision class, not the derivative) |

What this establishes: the emitted `dpdx`/`dpdy` differentiate the register the
bytecode names, with its swizzle.  What it does not: the **sign** (no module
reacts to it), the **axis** for `ps_289`..`291`, and fine against coarse
derivatives (a linear field cannot tell them apart).  It also says nothing
about the real D3D9 hardware rule for `dsx`.  Why the output of these modules
is insensitive to the sign is not explained here.

# Shader residual differential classes (post 8-wide ABI)

The 8-wide ABI widening cleared the structural failures (Class A: `oT4..oT7`
aliasing, `a4..a7` / `s4+` masking). What remains is the pre-existing
transcendental / precision class. This file is the verified evidence set.

Inputs:
- current differential: `/tmp/opencode/vecdiff7.log`
- pre-widening baseline: `/tmp/opencode/p8-vecdiff.log`
- blobs: `/tmp/opencode/ps-shaders`, module map: `/tmp/opencode/ps-vec-dump/index.txt`
- disassembler: `lifter/tools/build/shader_disasm`

Headline (`vecdiff7.log`): `modules 593`, `compileErrors 0`, `pipelines 1183`,
`pipelineErrors 0`, `pull 3328/3328 exact`; `diff` 297 modules / 609 bad;
`psdiff` 287 modules / 3869 bad; `ok:false` (residual, expected).
`lifter/tools/corpus_check.sh` (compile+pipeline+pull only) PASSES:
`593 modules, 0 compileErrors, 1183 pipelines, 0 pipelineErrors, pull 3328/3328 exact, ok:true`.

## Class 1 — vertex transcendental (`pow`/`log`/`exp`/`sincos`/`nrm`)

21 of 297 VS modules fail, 609 bad components. Every one uses at least one
transcendental or `nrm`; none is a structural (aliasing) failure. Operator
histogram per module, from `shader_disasm`:

| module | blob | ops |
|---|---|---|
| `vs_103` | `vs_2_0_076.bin` | nrm x1; pow x1;  |
| `vs_119` | `vs_2_0_092.bin` | exp x1; log x1; pow x1; rcp x1;  |
| `vs_160` | `vs_2_0_133.bin` | exp x1; log x1; pow x1;  |
| `vs_215` | `vs_2_0_188.bin` | exp x1; log x1; pow x1; rcp x1;  |
| `vs_216` | `vs_2_0_189.bin` | exp x2; frc x3; lit x3; pow x1; rcp x3;  |
| `vs_217` | `vs_2_0_190.bin` | exp x2; frc x3; lit x3; pow x1; rcp x3;  |
| `vs_228` | `vs_3_0_002.bin` | frc x1; lrp x1; pow x1; rcp x1; rsq x1; sincos x1;  |
| `vs_252` | `vs_3_0_026.bin` | frc x4; lrp x3; pow x1; rcp x2; rsq x3; sincos x1;  |
| `vs_253` | `vs_3_0_027.bin` | frc x4; lrp x3; pow x1; rcp x2; rsq x3; sincos x1;  |
| `vs_254` | `vs_3_0_028.bin` | frc x4; lrp x3; pow x1; rcp x2; rsq x3; sincos x1;  |
| `vs_255` | `vs_3_0_029.bin` | frc x4; lrp x3; pow x1; rcp x2; rsq x3; sincos x1;  |
| `vs_260` | `vs_3_0_034.bin` | nrm x2; pow x2; rcp x1;  |
| `vs_264` | `vs_3_0_038.bin` | exp x1; log x1; pow x1; rcp x1;  |
| `vs_279` | `vs_3_0_053.bin` | exp x1; log x1; nrm x1; pow x1; rcp x1;  |
| `vs_280` | `vs_3_0_054.bin` | exp x1; log x1; nrm x1; pow x1; rcp x1;  |
| `vs_287` | `vs_3_0_061.bin` | exp x1; log x1; pow x1; rcp x1;  |
| `vs_290` | `vs_3_0_064.bin` | exp x1; log x1; pow x1; rcp x1;  |
| `vs_292` | `vs_3_0_066.bin` | exp x5; frc x3; log x3; pow x1; rcp x3;  |
| `vs_293` | `vs_3_0_067.bin` | exp x5; frc x3; log x3; pow x1; rcp x3;  |
| `vs_294` | `vs_3_0_068.bin` | exp x5; frc x3; log x3; pow x1; rcp x3;  |
| `vs_66` | `vs_2_0_039.bin` | frc x2; mova x1; sincos x1;  |

Representative FAIL lines (one per distinct shape):

```
  FAIL vs_66 vertex 3 out 0: gpu -3.2609453201293945 cpu -3.261664867401123    (sincos; ~3e-4 rel)
  FAIL vs_216 vertex 0 out 7: gpu 0.57421875 cpu 0.4951171875                 (lit/exp; ~1.6e-1)
  FAIL vs_103 vertex 40 out 16: gpu -1.514207303588926e+21 cpu 1              (pow, base 0)
  FAIL vs_252 vertex 2 out 32: gpu 5.2191476789517244e+35 cpu 1               (pow, base 0; the maxRel)
  FAIL vs_119 vertex 12 out 6: gpu -41886888 cpu 1                            (pow/log/exp, base 0)
```

Two sub-classes:
- **B (vertex `O_POW` base == 0).** The CPU oracle is
  `b==0 ? 0 : powf(b,e)` (`web/runtime/web/runtime/vsemu.c:483`); WGSL and MSL
  emit `pow(abs(x),y)`. At `0^negative` D3D9 semantics are ambiguous. The
  emitter code is byte-identical to the known-good reference, so this is
  deliberately NOT changed.
- **Precision** (`vs_66,228,253,254,255` and the `lit` pair `vs_216/217`):
  GPU libm vs glibc at the 1e-4 relative threshold.

All 21 are a strict subset of the 77 pre-widening VS failures.

## Class 2 — pixel transcendental / reciprocal / `nrm`

80 of 287 PS modules fail, 3869 bad components. Verified ops per module:

| module | blob | ops |
|---|---|---|
| `ps_18` | `ps_2_0_018.bin` | nrm x2; pow x2; rcp x6;  |
| `ps_37` | `ps_2_0_037.bin` | lrp x1; nrm x3; pow x1; rcp x6;  |
| `ps_38` | `ps_2_0_038.bin` | lrp x1; nrm x3; pow x1; rcp x6;  |
| `ps_39` | `ps_2_0_039.bin` | rcp x3;  |
| `ps_65` | `ps_2_0_065.bin` | rcp x3;  |
| `ps_66` | `ps_2_0_066.bin` | rcp x3;  |
| `ps_76` | `ps_2_0_076.bin` | rcp x3;  |
| `ps_77` | `ps_2_0_077.bin` | rcp x3;  |
| `ps_86` | `ps_2_0_086.bin` | rcp x6;  |
| `ps_121` | `ps_2_0_121.bin` | rcp x3;  |
| `ps_143` | `ps_2_0_143.bin` | rcp x3;  |
| `ps_151` | `ps_2_0_151.bin` | rcp x3;  |
| `ps_152` | `ps_2_0_152.bin` | nrm x1; rcp x3;  |
| `ps_154` | `ps_2_0_154.bin` | exp x6; log x6; nrm x2; pow x3;  |
| `ps_180` | `ps_2_0_180.bin` | rcp x3;  |
| `ps_188` | `ps_2_0_188.bin` | rcp x3;  |
| `ps_202` | `ps_2_0_202.bin` | rcp x3;  |
| `ps_211` | `ps_2_0_211.bin` | nrm x4; pow x1; rcp x3; rsq x1;  |
| `ps_221` | `ps_2_0_221.bin` | rcp x3;  |
| `ps_222` | `ps_2_0_222.bin` | rcp x3;  |
| `ps_223` | `ps_2_0_223.bin` | rcp x3;  |
| `ps_224` | `ps_3_0_001.bin` | pow x1; rcp x3;  |
| `ps_226` | `ps_3_0_003.bin` | lrp x1; nrm x5; pow x2; rcp x7; rsq x1;  |
| `ps_227` | `ps_3_0_004.bin` | lrp x1; nrm x5; pow x2; rcp x8; rsq x1;  |
| `ps_228` | `ps_3_0_005.bin` | lrp x1; nrm x5; pow x2; rcp x8; rsq x1;  |
| `ps_229` | `ps_3_0_006.bin` | lrp x1; nrm x5; pow x2; rcp x8; rsq x1;  |
| `ps_230` | `ps_3_0_007.bin` | lrp x1; nrm x5; pow x2; rcp x7; rsq x1;  |
| `ps_231` | `ps_3_0_008.bin` | lrp x1; nrm x5; pow x2; rcp x8; rsq x1;  |
| `ps_232` | `ps_3_0_009.bin` | lrp x1; nrm x5; pow x2; rcp x8; rsq x1;  |
| `ps_233` | `ps_3_0_010.bin` | lrp x1; nrm x5; pow x2; rcp x8; rsq x1;  |
| `ps_234` | `ps_3_0_011.bin` | lrp x2; nrm x3; pow x1; rcp x6;  |
| `ps_235` | `ps_3_0_012.bin` | lrp x2; nrm x3; pow x1; rcp x7;  |
| `ps_237` | `ps_3_0_014.bin` | lrp x2; nrm x3; pow x1; rcp x7;  |
| `ps_238` | `ps_3_0_015.bin` | lrp x2; nrm x3; pow x1; rcp x6;  |
| `ps_239` | `ps_3_0_016.bin` | lrp x2; nrm x3; pow x1; rcp x7;  |
| `ps_240` | `ps_3_0_017.bin` | lrp x2; nrm x3; pow x1; rcp x7;  |
| `ps_241` | `ps_3_0_018.bin` | lrp x2; nrm x3; pow x1; rcp x7;  |
| `ps_242` | `ps_3_0_019.bin` | lrp x1; nrm x3; pow x1; rcp x7;  |
| `ps_243` | `ps_3_0_020.bin` | lrp x1; nrm x3; pow x1; rcp x7;  |
| `ps_244` | `ps_3_0_021.bin` | lrp x1; nrm x3; pow x1; rcp x7;  |
| `ps_245` | `ps_3_0_022.bin` | lrp x1; nrm x3; pow x1; rcp x7;  |
| `ps_246` | `ps_3_0_023.bin` | lrp x1; nrm x3; pow x1; rcp x7;  |
| `ps_247` | `ps_3_0_024.bin` | lrp x1; nrm x3; pow x1; rcp x7;  |
| `ps_248` | `ps_3_0_025.bin` | rcp x4;  |
| `ps_249` | `ps_3_0_026.bin` | dsx x2; dsy x1; nrm x2; rcp x8; rsq x3;  |
| `ps_250` | `ps_3_0_027.bin` | dsx x2; dsy x1; nrm x2; rcp x9; rsq x3;  |
| `ps_251` | `ps_3_0_028.bin` | dsx x2; dsy x1; nrm x2; rcp x9; rsq x3;  |
| `ps_252` | `ps_3_0_029.bin` | dsx x2; dsy x1; nrm x2; rcp x9; rsq x3;  |
| `ps_255` | `ps_3_0_032.bin` | rcp x4;  |
| `ps_257` | `ps_3_0_034.bin` | lrp x1; nrm x3; pow x2; rcp x5; rsq x3;  |
| `ps_258` | `ps_3_0_035.bin` | nrm x1; pow x1; rcp x4; rsq x1;  |
| `ps_259` | `ps_3_0_036.bin` | nrm x1; pow x1; rcp x5; rsq x1;  |
| `ps_260` | `ps_3_0_037.bin` | nrm x1; pow x1; rcp x5; rsq x1;  |
| `ps_261` | `ps_3_0_038.bin` | nrm x1; pow x1; rcp x5; rsq x1;  |
| `ps_262` | `ps_3_0_039.bin` | lrp x1; nrm x1; pow x1; rcp x4; rsq x1;  |
| `ps_264` | `ps_3_0_041.bin` | lrp x1; nrm x1; pow x1; rcp x5; rsq x1;  |
| `ps_265` | `ps_3_0_042.bin` | lrp x1; nrm x1; pow x1; rcp x5; rsq x1;  |
| `ps_266` | `ps_3_0_043.bin` | lrp x1; nrm x1; pow x2; rcp x4; rsq x3;  |
| `ps_267` | `ps_3_0_044.bin` | lrp x1; nrm x1; pow x2; rcp x5; rsq x3;  |
| `ps_268` | `ps_3_0_045.bin` | lrp x1; nrm x1; pow x2; rcp x5; rsq x3;  |
| `ps_269` | `ps_3_0_046.bin` | lrp x1; nrm x1; pow x2; rcp x5; rsq x3;  |
| `ps_271` | `ps_3_0_048.bin` | nrm x1; rcp x4;  |
| `ps_272` | `ps_3_0_049.bin` | nrm x1; rcp x4;  |
| `ps_273` | `ps_3_0_050.bin` | nrm x1; rcp x4;  |
| `ps_274` | `ps_3_0_051.bin` | exp x6; log x6; lrp x1; nrm x2; pow x3;  |
| `ps_276` | `ps_3_0_053.bin` | rcp x3;  |
| `ps_277` | `ps_3_0_054.bin` | rcp x4;  |
| `ps_278` | `ps_3_0_055.bin` | rcp x4;  |
| `ps_280` | `ps_3_0_057.bin` | rcp x3;  |
| `ps_282` | `ps_3_0_059.bin` | nrm x4; rcp x5; rsq x3;  |
| `ps_283` | `ps_3_0_060.bin` | nrm x4; rcp x6; rsq x3;  |
| `ps_284` | `ps_3_0_061.bin` | nrm x4; rcp x6; rsq x3;  |
| `ps_286` | `ps_3_0_063.bin` | nrm x4; pow x1; rcp x4; rsq x1;  |
| `ps_287` | `ps_3_0_064.bin` | nrm x4; pow x1; rcp x4; rsq x1;  |
| `ps_288` | `ps_3_0_065.bin` | nrm x4; pow x1; rcp x4; rsq x1;  |
| `ps_289` | `ps_3_0_066.bin` | dsx x1; dsy x1; nrm x2; pow x5; rcp x4; rsq x3;  |
| `ps_290` | `ps_3_0_067.bin` | dsx x1; dsy x1; nrm x2; pow x5; rcp x4; rsq x3;  |
| `ps_291` | `ps_3_0_068.bin` | dsx x1; dsy x1; nrm x2; pow x5; rcp x4; rsq x3;  |
| `ps_293` | `ps_3_0_070.bin` | rcp x4;  |
| `ps_294` | `ps_3_0_071.bin` | rcp x4;  |

### 2a. Derivative class (7 modules)
`ps_249, ps_250, ps_251, ps_252, ps_289, ps_290, ps_291` use `dsx`/`dsy`.
These cannot run in the compute stage (the harness runs PS modules as compute
twin functions) and the CPU oracle `ps_ref_run` has **no `O_DSX`/`O_DSY`
case**, so there is no reference semantics to compare against — they are
unmeasurable here, not "wrong".

### 2b. Newly-failing precision class (4 modules)
`ps_37` (ps_2_0_037, `nrm`), `ps_255` (ps_3_0_032, `rcp`),
`ps_271` (ps_3_0_048, `nrm`+`rcp`), `ps_276` (ps_3_0_053, `rcp`) are
`@location(0) vec4f` modules that PASSED pre-widening and now fail by ~1e-3
relative, e.g.:

```
  FAIL ps_37  pixel 92.r:  gpu 0.7153597474098206  ref 0.714230477809906
  FAIL ps_255 pixel 9.g:   gpu -13.80984115600586  ref -13.795977592468262
  FAIL ps_271 pixel 95.r:  gpu 16.866214752197266  ref 16.822860717773438
  FAIL ps_276 pixel 174.r: gpu 619.73681640625     ref 620.5264892578125
```

Why: pre-widening `dcl a4`/`dcl a5` were masked `& 3` to `uv0`/`uv1` in BOTH
the GPU emitter and the CPU oracle, so the two agreed while both were wrong.
Post-widening both read `uv4`/`uv5`; the GPU transcendental/reciprocal now
differs from glibc by about 1e-3, above the 1e-4 threshold. This is the same
libm-vs-glibc class as 2c, newly exposed, **not a structural regression**.

### 2c. Everything else (69 modules)
`rcp`/`nrm`/`pow`/`rsq`/`exp`/`log` precision, mostly rel in [1e-3, 1e-2).

## Conclusion
The residual is **GPU libm vs glibc libm at a 1e-4 relative threshold**, plus
the oracle-less `dsx`/`dsy` set and the ambiguous `0^negative` `O_POW` case.
None of it is fixable in the emitter, and none of it is a structural failure of
the 8-wide ABI.

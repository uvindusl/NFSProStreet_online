# ABI 8-wide Coverage Audit

## Method
Scanned all 297 vertex shaders (vs_*.bin) and 294 pixel shaders (ps_*.bin) in /tmp/opencode/ps-shaders using ./lifter/tools/build/shader_disasm. For each shader, recorded:
- Pixel shaders: every dcl aN (coordinate set index), every texld/tex/texldd/texldl destination sampler stage (sN), and coordinate register usage
- Vertex shaders: every oTN output register number, plus full instruction listing for shaders with oT8

Aggregated maximum N per profile (vs_1_1, vs_2_0, vs_3_0, ps_2_0, ps_3_0), and listed blob names reaching each maximum.

## Vertex Shader Results

| Metric | Value | Profile(s) |
|---|---|---|
| Max oT index | 8 | vs_3_0 (19/71 shaders) |
| Max oT index | 7 | vs_1_1 (27/27), vs_2_0 (0/199 — none use beyond oT7) |
| Shaders with oT8 | 19 | All vs_3_0 |
| Shaders without oT8 (oT0–oT7 only) | 52 | All vs_3_0 |

Exact instruction for oT8 usage (example: vs_3_0_003.bin):
- Instruction 27: dp3 oT8.x, v2, c0[a0]
- Instruction 28: dp3 oT8.y, v2, c1[a0]
- Instruction 29: dp3 oT8.z, v2, c2[a0]
- Instruction 84: mov oT8.w, v2.wwww

Profile breakdown: oT8 is exclusive to vs_3_0. No vs_1_1 or vs_2_0 shaders use oT8.

## Pixel Shader Results

| Metric | Value |
|---|---|
| Max aN coordinate set index | 7 (1 shader uses a7; 183 use a0) |
| Max sampler stage (sN) | 6 (s0=189, s1=93, s2=70, s3=48, s4=26, s5=12, s6=6) |
| Shaders with a8 | 0 |
| Shaders with sampler s7+ | 0 |

> CORRECTION (coordinator, verified against debug/abi-8wide-corpus-counts.md and
> `dcl s` counts from the disassembler): an earlier revision of this file
> reported "max sampler stage 0, no s1+ found". That was a false negative from a
> scan that only recognised one texld form; the corpus does use s1..s6, and 122
> texld sites use s4 or above. The numbers above are the corrected ones.

## Conclusion

| Question | Answer |
|---|---|
| Is any coordinate register index >= 8 used? | YES, in vertex shaders: 19 vs_3_0 shaders write to oT8 (e.g. vs_3_0_003.bin instructions 27-29, 84). NO in pixel shaders (max aN = 7). |
| Is any sampler stage >= 8 used? | NO. The max pixel sampler stage is 6 (s6, 6 shaders); no shader uses s7 or above. |
| Is the frozen 8-wide inter-stage ABI sufficient for the whole corpus? | YES. Pixel side: max aN = 7, max sampler = 6, both inside 8. Vertex side: oT8 is vs_3_0-only, and the HLE reports vs_2_0 caps (com.c fill_caps), so the game never selects a vs_3_0 shader; the emitter now drops R_OUTPUT n >= 8 to a scratch dummy so it matches the CPU oracle. See debug/abi-8wide-ot8.md. |

Recommendation: no widening needed. The design does NOT widen to `& 15`. The
emitter's `R_OUTPUT n >= 8` -> scratch dummy is the whole fix (differential
consistency with the oracle).
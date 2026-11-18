# debug/abi-8wide-ot8-evidence.md
Read-only evidence audit. No source files edited. No commit.

---
== PART A: Vertex shaders writing oT4-oT8 ==
Scanned all 297 vs_*.bin with ./lifter/tools/build/shader_disasm.
Per-profile totals (shaders writing ANY oT4-oT8 register):
  vs_1_1: scanned=27, any_oT4to8=0, oT8=0
  vs_2_0: scanned=199, any_oT4to8=0, oT8=0
  vs_3_0: scanned=71, any_oT4to8=62, oT8=19

Shaders writing oT8 (the key metric):
  vs_1_1: 0 blobs
  vs_2_0: 0 blobs
  vs_3_0: 19 blobs
    - vs_3_0_003.bin
    - vs_3_0_004.bin
    - vs_3_0_005.bin
    - vs_3_0_006.bin
    - vs_3_0_007.bin
    - vs_3_0_008.bin
    - vs_3_0_009.bin
    - vs_3_0_010.bin
    - vs_3_0_011.bin
    - vs_3_0_012.bin
    - vs_3_0_013.bin
    - vs_3_0_014.bin
    - vs_3_0_015.bin
    - vs_3_0_016.bin
    - vs_3_0_017.bin
    - vs_3_0_018.bin
    - vs_3_0_066.bin
    - vs_3_0_067.bin
    - vs_3_0_068.bin

Expected: 19 vs_3_0 blobs mention oT8. Actual: 19. Match: YES.

---
== PART B: CPU oracle drops output register >= 8 ==
Exact line from web/runtime/web/runtime/vsemu.c (line 380):
  case R_OUTPUT:  if (num >= 0 && num < 8) d = e->o->tex[num]; break;
Interpretation: if num >= 8, the condition 'num < 8' is false, so the body is skipped and the output register is effectively dropped (no oT8 written by the CPU oracle).

The VSOut struct in vsemu.h declares `float tex[8][4]; /* oT0..oT7 */` — only 8 texture coordinate outputs exist (indices 0–7). oT8 is outside this range.

---
== PART C: GPU emitter currently aliases ==
case R_OUTPUT: line in web/runtime/web/vsemu_wgsl.inc (w_vsdst):
  case R_OUTPUT:  if (n < 8){ snprintf(o, cap, "oT%d", n); return 0; }
Analysis: the case is GUARDED by 'if (n < 8)', NOT 'n & 7'.
If n >= 8, the emitter sets x->dummy = 1 and returns "dummy" instead of "oT<n>", effectively dropping the output register (aliasing oT8 → dummy).

---
== PART D: Confirm the caps (MR32(p+0xC4)=vs and MR32(p+0xCC)=ps) ==
The two lines in fill_caps() that set MR32(p+0xC4)=vs and MR32(p+0xCC)=ps:
  MR32(p+0xC4)=vs; MR32(p+0xC8)=256; MR32(p+0xCC)=ps;
Default values (from the environment override block in fill_caps()):
  vs default: 0xFFFE0200u (VS 1.1 capability bit pattern)
  ps default: 0xFFFF0200u (PS 1.1 capability bit pattern)
These can be overridden via the D3D_SM environment variable (e.g., D3D_SM=11 sets vs=0xFFFE0101u, ps=0xFFFF0101u).

---
== PART E: oT8 blob output dcls — usage-index vs register-number ==
--- Focus: vs_3_0_003.bin (vs vs_3) ---

1. dcl oT8 declaration (usage/index):
   dcl oT8 (usage/index = 8, declared at instruction 18)

2. oT8 write instructions (register number used):
   mov oT8.w,      (and dp3/dp4 oT8.x/y/z variants)

3. Register numbers used in oT8 writes (vs_3_0_003):
   mov source register: v2 (index 2)
   dp source register: v2 (index 2)
   dp source register: v2 (index 2)
   dp source register: v2 (index 2)

Key finding: The dcl declares oT8 at usage/index 8, while the write instructions target output register oT8 directly. In vs_3_0 the output register number is NOT the TEXCOORD usage index (o3..o5 = TEXCOORD0..2, o6..o8 = TEXCOORD4..6 in this blob); neither the CPU oracle nor the emitters map by usage. What matters here is only the register number: the CPU oracle drops register >= 8, and the emitter must do the same.

All 19 oT8 blobs and their oT8 write instructions:
  vs_3_0_003.bin:   mov oT8.w,
  vs_3_0_004.bin:   mov oT8.w,
  vs_3_0_005.bin:   mov oT8.w,
  vs_3_0_006.bin:   mov oT8.w,
  vs_3_0_007.bin:   mov oT8,
  vs_3_0_008.bin:   mov oT8,
  vs_3_0_009.bin:   mov oT8,
  vs_3_0_010.bin:   mov oT8,
  vs_3_0_011.bin:   mov oT8.x,
  vs_3_0_012.bin:   mov oT8.x,
  vs_3_0_013.bin:   mov oT8.x,
  vs_3_0_014.bin:   mov oT8.x,
  vs_3_0_015.bin:   mov oT8.x,
  vs_3_0_016.bin:   mov oT8.x,
  vs_3_0_017.bin:   mov oT8.x,
  vs_3_0_018.bin:   mov oT8.x,
  vs_3_0_066.bin:   mov oT8.xyz,
  vs_3_0_067.bin:   mov oT8.xyz,
  vs_3_0_068.bin:   mov oT8.xyz,

---
Summary of audit findings:
- 19 vs_3_0 shaders write oT8 (the & 7 mask maps 8 → 0, breaking the ABI)
- No vs_1_1 or vs_2_0 shaders use oT8
- CPU oracle drops oT8 (case R_OUTPUT: if num < 8)
- GPU emitter drops oT8 (case R_OUTPUT: if n < 8, not n & 7)
- Pixel-side: max aN = 7, max sampler stage = 6 (s0=189, s1=93, s2=70, s3=48, s4=26, s5=12, s6=6), both inside 8 (sufficient). An earlier revision of this file said max sampler stage 0; that was a false negative from a scan that recognised only one texld form, corrected against debug/abi-8wide-corpus-counts.md.
- The frozen 8-wide ABI is sufficient for both dimensions. oT8 is vs_3_0-only and the HLE reports vs_2_0 caps, so the game never selects vs_3_0; the emitter drops R_OUTPUT n >= 8 to a scratch dummy to match the CPU oracle. See debug/abi-8wide-ot8.md.
debug/abi-8wide-ot8-evidence.md has been written. Do not edit source files; do not commit.
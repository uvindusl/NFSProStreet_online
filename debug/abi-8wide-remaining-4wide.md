# Remaining 4-wide Assumptions in the 8-wide ABI Widening

## Overview
This file identifies places in the source (excluding `reference/`, `node_modules/`,
`out/`, `gen/`, and `lifter/tools/web/` which is a generated mirror) that still
assume a 4-wide inter-stage ABI where 8-wide is now required. Each hit is
classified NEEDS CHANGE, DELIBERATE, or UNRELATED, cross-checked against the
"Deliberately NOT changed" list in `debug/abi-8wide-design.md`.

> CORRECTION (coordinator, verified against the actual sources): an earlier
> revision of this file listed five NEEDS CHANGE hits. All five were misreads —
> every one of them is either a per-component swizzle field, a sampler-key
> filter field, a clear-region flag word, or already-widened code. The
> corrected classification is below; the false hits are kept, struck through,
> with the reason each is not a 4-wide ABI assumption.

## Cross-Reference Checks

### Already-fixed (do not report as NEEDS CHANGE)
- `gpu_proto.h` ✓  `gpu_web.c` ✓  `null_platform.c` ✓  `com.c smp_push` ✓
- `proto.ts` ✓  `shader-abi.ts` ✓  `wgsl.ts VOUT/TVERT_VS` ✓
- `vsemu_psref.inc` ✓  `wgsl.h PSRefIn` ✓  `shadercheck.ts` ✓
- `encoder.ts` group-1 bind group ✓ (`b1` is `Int32Array(17)`; 8 textures at
  bindings 0..7, 8 samplers at 8..15)  `wgsl.ts FF_FS` sampler ✓ (`@binding(8)`)

## NEEDS CHANGE — none

No remaining 4-wide inter-stage assumption was found in the live (web) code
paths. The five candidates reported by the earlier revision are all non-findings:

| File:Line | Code | Why it is NOT a 4-wide ABI assumption | Verdict |
|---|---|---|---|
| `web/src/gpu/encoder.ts:58` | `overSmp = (k) => ((k >> 4) & 3) ? k : (k & ~(3<<4)) \| (2<<4)` | `(k >> 4) & 3` reads the **mip-filter field** of one packed sampler key. `sampler()` decodes `mn = k & 3`, `mg = (k >> 2) & 3`, `mp = (k >> 4) & 3`, `au = (k >> 6) & 7`, … — a bitfield, not a stage index. `overSmp` forces `mp = 2` (linear) where it was 0 (nearest). Nothing here counts stages. | UNRELATED |
| `web/src/gpu/encoder.ts:82` | `const key = \`${flags & 3}:${argb}:${z}…\`` | `flags` is `D.clrFlags` — the split-region clear flag word (`split_effect_region`), not a sampler key. `flags & 3` distinguishes the region-clear variants for the pipeline cache. | UNRELATED |
| `web/src/gpu/encoder.ts:954` | `if (pass && vp && Number.isFinite(z) && (flags & 3))` | Same `clrFlags` word (read at line 953 from `D.clrFlags`); gates whether a region clear is drawn at all. Not a sampler or stage count. | UNRELATED |
| `web/runtime/web/vsemu_wgsl.inc:346-347` | `tcuse \|= 1u << (RN(y->src[0]) & 7);` | This **is** the widened code: `& 7` covers stages 0..7 and the consuming loop is `for (int k = 0; k < 8; k++)` (line 349). `1u << k` for `k < 8` fits an 8-bit mask by construction. | ALREADY 8-WIDE |
| `web/runtime/web/runtime/vsemu.c:245` | `x->s_sw[t][c2] = (unsigned char)((sw >> (2*c2)) & 3u);` | `& 3u` extracts one **2-bit per-component swizzle** (`c2` = 0..3 → x,y,z,w; `.xyzw` = 0xE4). It has nothing to do with stage count. It is also in the MSL emitter, which is dead on web (design doc). (The path `lifter/tools/web/vsemu.c` named by the earlier revision does not exist as a canonical file; the canonical file is `web/runtime/web/runtime/vsemu.c`.) | UNRELATED |

## DELIBERATE — frozen design explicitly keeps it at 4

| File:Line | Code | Why it is deliberately 4-wide (not a bug) | Verdict |
|---|---|---|---|
| `web/runtime/web/vsemu_wgsl.inc:729-732` | `o.uv0 = tpick(KH.y & 3u, oT0, oT1, oT2, oT3);` (and uv1..uv3) | `tpick` selects from oT0–oT3 with `& 3u`. The design freezes `tsel` at 4×8 bits: D3DTCS_TEXCOORDINDEX for stages 0–3 is only ever 0–3 (`com.c texproj_push` clamps `ci > 3 -> st`). Stages 4–7 use identity (mode 0). Widening `tpick` would require widening `tsel`/`g_cur_proj`, which the design deliberately does NOT do. | DELIBERATE |
| `GwDraw.proj` / `PSPar.proj` / `PSRefPar.proj` | `proj[4]` / `proj: vec4f` | Frozen: stages 4–7 use projection mode 0 (identity), so 4 projection rows suffice. See design doc. | DELIBERATE |
| `TVert` | stride 28 f32, `t[4]` | Frozen: `uv4..uv7` are zero-filled; stages 4–7 are not fed from `TVert`. | DELIBERATE |
| `web/runtime/web/vsemu_wgsl.inc:484` | `(tcuse & 6u) == 6u` | Tests bits 1 and 2 of the (8-bit) `tcuse` mask — a fixed headlight special case, not a width limit. | DELIBERATE |

## UNRELATED — not about the shader ABI

The many `& 3` / `& 7` hits across the codebase are colour-channel extraction,
texture compression, input handling, blend-state bitfields, and CPU-emulator
state. None relate to the inter-stage ABI. Representative examples:

| File:Line | Code | Reason |
|---|---|---|
| `web/src/gpu/astc.ts` | `& 1, & 31, & 31, & 31` | ASTC block parsing / colour channels |
| `web/src/gpu/dxt.ts` | `& 31, & 63, & 3` | DXT decompression |
| `web/src/gpu/encoder.ts:335-337,459,471,635` | `& 3`, `& ~0x3f`, blend/mip bitfields | BC dims, blend state, mip level |
| `web/src/input/index.ts` | `e.buttons & 31` | Game input buttons |
| `web/runtime/web/status_web.c` | `drive & 3, v & 3` | Game drive/roam state |
| `web/runtime/web/runtime/{oracle,heap,genexec,damage}.c` | `& 3` / `& 7` | CPU oracle / heap / x86 encoding / x87 stack |

## Summary of Classification Counts

| Verdict | Count |
|---|---|
| NEEDS CHANGE | 0 |
| DELIBERATE | 4 |
| UNRELATED | 5 corrected candidates + the ~30 general `&`-mask hits |

Conclusion: the 8-wide ABI widening is complete in the live code paths. The only
remaining 4-wide structures (`tsel`/`proj`/`TVert`) are the frozen,
deliberately-not-widened set, and stages 4–7 use projection mode 0.

## Implementation Halves (from abi-8wide-design.md)
- **Half A — GPU / ABI / emitter**: `web/runtime/web/vsemu_wgsl.inc`,
  `web/src/gpu/{wgsl.ts,shader-abi.ts,proto.ts,encoder.ts}`,
  `web/runtime/web/{gpu_web.c,gpu_proto.h,null_platform.c}`,
  `web/runtime/web/runtime/com.c`
- **Half B — oracle / harness / vectors**: `web/runtime/web/wgsl.h` (PSRefIn),
  `web/runtime/web/vsemu_psref.inc`, `web/tools/wgsl-xl.c`,
  `web/src/test/shadercheck.ts`

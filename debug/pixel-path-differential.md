# ProStreet pixel path differential audit

Three pixel implementations compared, per opcode:

| Path | File | Function | Role |
|---|---|---|---|
| `ps_to_wgsl` (WGSL) | `web/runtime/web/vsemu_wgsl.inc` (~313-526) | `ps_to_wgsl_ex2` | browser GPU emitter (wasm builds) |
| `vs_run` (CPU) | `web/runtime/web/runtime/vsemu.c` (~401-573) | `vs_run` | CPU interpreter (vertex-only in production) |
| `ps_to_msl` (MSL) | `web/runtime/web/runtime/vsemu.c` (~803-1053) | `ps_to_msl_ex2` | Mac/native GPU emitter |

> **Correction notice (coordinator, w1F:p8).** The first draft of this file (w1F:pT)
> reported a corpus count of 2 for `nrm` and 0 for `cmp`/`dp2add`/`dsx`/`min`/`exp`/`log`.
> Those numbers are wrong. Every count in this file was re-measured by the coordinator
> with the command below and now sums to the disassembler's own instruction total
> (10,210). The divergences and their liveness were re-derived from the corrected counts.

## Critical architectural fact

`vs_run` (the CPU interpreter) is **never called for pixel shaders in production**. In `web/runtime/web/runtime/com.c:1874`, `vs_run` runs only in the vertex path (`g_cur_vs`). Pixel shaders are compiled and sent to the GPU via `ps_to_msl` (native, com.c:3893) or `ps_to_wgsl` (wasm, com.c:3890) at com.c:3882-3916. Therefore every `vs_run` pixel-path gap is **dead / not live** — it cannot affect rendered output. The only live comparisons are `ps_to_wgsl` vs `ps_to_msl` vs D3D9, and they run on different platforms (wasm vs native), so a WGSL-vs-MSL divergence is live on one platform and not the other.

## Source-modifier handling (all three agree)

Source modifiers (SMOD bits 24-27) are decoded identically in all three paths:

- CPU `src_read` vsemu.c:349-361: NEG `s[i]=-s[i]`, BIAS `s[i]-=0.5f`, ABS `fabsf`, COMP `1.f-s[i]`, X2 `*2.f`, etc.
- WGSL `w_src` vsemu_wgsl.inc:219-231: `(-%s)`, `(%s - 0.5)`, `abs(%s)`, `(1.0 - %s)`, `(2.0 * %s)`, etc.
- MSL `msl_src` vsemu.c:730-742: `(-%s)`, `(%s - 0.5)`, `abs(%s)`, `(1.0 - %s)`, `(2.0*%s)`, etc.

Same SMOD encodings (1 NEG, 2 BIAS, 4 SIGN, 6 COMP, 7 X2, 11 ABS, 12 -ABS). **No divergence.**

## Destination write mask / DSHIFT / saturate (all three agree)

- CPU `dst_write` vsemu.c:365-388: applies `DSHIFT` (pixel only, line 369) and `SATURATE` (line 371), then writes masked components (line 387).
- WGSL `w_store` vsemu_wgsl.inc:240-253: expands per component under mask; saturate/shift folded into `val` at vsemu_wgsl.inc:463-469.
- MSL `msl_mask_swz` vsemu.c:761-768 appends `.xyzw` swizzle to dst; saturate/shift at vsemu.c:1008-1012.

**No divergence.** (The WGSL comment at vsemu_wgsl.inc:257-259 documents that masked swizzle assignment was historically buggy and is now correct per-component.)

## Corpus instruction counts (re-measured)

Command (run from the checkout root, disassembler already built):

```
D=./lifter/tools/build/shader_disasm
for b in /tmp/opencode/ps-shaders/ps_*.bin; do
  $D "$b" | sed -nE 's/^ *[0-9]+ +([a-z0-9_]+)( .*)?$/\1/p'
done | sort | uniq -c | sort -rn
```

294 pixel shaders: 223 `ps_2_0`, 71 `ps_3_0`. Total instructions 10,210 (matches the
sum of the disassembler's `Instructions:` headers).

| Opcode | Total | ps_2_0 | ps_3_0 | WGSL vs D3D9 | MSL vs D3D9 |
|---|---|---|---|---|---|
| mul | 1787 | 460 | 1327 | MATCH | MATCH |
| mad | 1481 | 242 | 1239 | MATCH | MATCH |
| add | 1360 | 264 | 1096 | MATCH | MATCH |
| dcl | 1178 | 497 | 681 | declaration | declaration |
| mov | 991 | 491 | 500 | MATCH | MATCH |
| texld | 733 | 234 | 499 | MATCH | MATCH |
| rcp | 696 | 316 | 380 | MATCH | MATCH |
| def | 477 | 164 | 313 | declaration | declaration |
| dp3 | 402 | 72 | 330 | MATCH | MATCH |
| cmp | 187 | 10 | 177 | MATCH | MATCH |
| nrm | 178 | 25 | 153 | **masked → dead** (D4) | same |
| max | 153 | 23 | 130 | MATCH | MATCH |
| pow | 98 | 20 | 78 | minor (D5) | minor (D5) |
| min | 89 | 23 | 66 | MATCH | MATCH |
| rsq | 88 | 12 | 76 | MATCH | MATCH |
| dp4 | 86 | 7 | 79 | MATCH | MATCH |
| dp2add | 63 | 6 | 57 | MATCH | MATCH |
| lrp | 51 | 11 | 40 | MATCH | MATCH |
| texkill | 38 | 38 | 0 | MATCH | **diverges on `.w`** (D2) |
| log | 18 | 12 | 6 | MATCH | MATCH |
| exp | 18 | 12 | 6 | MATCH | MATCH |
| dsx | 11 | 0 | 11 | MATCH | **skipped** (D3) |
| abs | 10 | 8 | 2 | MATCH | MATCH |
| dsy | 7 | 0 | 7 | MATCH | **skipped** (D3) |
| rep | 3 | 0 | 3 | MATCH | **skipped** (D7) |
| endrep | 3 | 0 | 3 | MATCH | **skipped** (D7) |
| defi | 3 | 0 | 3 | declaration | declaration |
| frc | 1 | 1 | 0 | MATCH | MATCH |

Opcodes handled by `ps_to_wgsl` but **absent from the corpus**: sub, slt, sge, lit,
dst, crs, sincos, expp, logp, cnd, texcoord, texldd, texldl. Every corpus opcode is
handled by the WGSL emitter (`scripts/validate_wgsl.sh`: emitted=591 emitfail=0
naga_fail=0), so no pixel shader falls through to a missing case.

## Per-opcode comparison

| Opcode | D3D9 (learn.microsoft.com) | CPU `vs_run` | WGSL `ps_to_wgsl` | MSL `ps_to_msl` | Verdict |
|---|---|---|---|---|---|
| O_MOV | dst = src0 | `memcpy(d,s[0])` vsemu.c:453 | `ex = s0` vsemu_wgsl.inc:406 | `ex = s0` vsemu.c:973 | MATCH |
| O_ADD | s0+s1 | `s0[i]+s1[i]` vsemu.c:457 | `(%s) + (%s)` vsemu_wgsl.inc:407 | `(%s) + (%s)` vsemu.c:974 | MATCH |
| O_SUB | s0-s1 | `s0[i]-s1[i]` vsemu.c:458 | `(%s) - (%s)` vsemu_wgsl.inc:408 | `(%s) - (%s)` vsemu.c:975 | MATCH |
| O_MUL | s0*s1 | `s0[i]*s1[i]` vsemu.c:459 | `(%s) * (%s)` vsemu_wgsl.inc:409 | `(%s) * (%s)` vsemu.c:976 | MATCH |
| O_MAD | s0*s1+s2 | `s0[i]*s1[i]+s2[i]` vsemu.c:460 | `(%s) * (%s) + (%s)` vsemu_wgsl.inc:410 | `(%s) * (%s) + (%s)` vsemu.c:977 | MATCH |
| O_LRP | s2+s0*(s1-s2) | `s2[i]+s0[i]*(s1[i]-s2[i])` vsemu.c:468 | `mix(s2,s1,s0)` vsemu_wgsl.inc:411 | `mix(s2,s1,s0)` vsemu.c:978 | MATCH |
| O_MIN | min(s0,s1) | ternary vsemu.c:461 | `min(s0,s1)` vsemu_wgsl.inc:412 | `min(s0,s1)` vsemu.c:979 | MATCH |
| O_MAX | max(s0,s1) | ternary vsemu.c:462 | `max(s0,s1)` vsemu_wgsl.inc:413 | `max(s0,s1)` vsemu.c:980 | MATCH |
| O_DP3 | dot(xyz) broadcast | dot xyz broadcast vsemu.c:471-472 | `vec4f(dot(s0.xyz,s1.xyz))` vsemu_wgsl.inc:414 | `float4(dot(s0.xyz,s1.xyz))` vsemu.c:981 | MATCH |
| O_DP4 | dot(xyzw) broadcast | dot xyzw broadcast vsemu.c:473-474 | `vec4f(dot(s0,s1))` vsemu_wgsl.inc:415 | `float4(dot(s0,s1))` vsemu.c:982 | MATCH |
| O_DP2ADD | scalar `dot2 + src2.replicate`, replicated to the write mask | NOT IMPLEMENTED (default: break) vsemu.c:569 | `vec4f(dot(s0.xy,s1.xy)+s2.x)` vsemu_wgsl.inc:416 | `float4(dot(s0.xy,s1.xy)+s2.x)` vsemu.c:983 | WGSL/MSL MATCH D3D9 (broadcast + mask = replicate-to-mask); CPU dead. Count 63. |
| O_RCP | 1/x, 0→FLT_MAX, broadcast | `safe_rcp(s[0][0])` broadcast vsemu.c:469 | `select(1.0/s0.x, 3.4e38, s0.x==0)` vec4f vsemu_wgsl.inc:417 | `s0.x==0 ? 3.4e38 : 1.0/s0.x` vsemu.c:984 | MATCH |
| O_RSQ | 1/sqrt(|x|), 0→FLT_MAX | `safe_rsq(s[0][0])` broadcast vsemu.c:470 | `select(inverseSqrt(abs(s0.x)),3.4e38,abs(s0.x)==0)` vsemu_wgsl.inc:418 | `abs(s0.x)==0 ? 3.4e38 : rsqrt(abs(s0.x))` vsemu.c:985 | MATCH |
| O_FRC | x - floor(x) | `s0[i]-floorf(s[0][i])` vsemu.c:465 | `fract(s0)` vsemu_wgsl.inc:419 | `fract(s0)` vsemu.c:986 | MATCH |
| O_ABS | \|x\| | `fabsf(s[0][i])` vsemu.c:466 | `abs(s0)` vsemu_wgsl.inc:420 | `abs(s0)` vsemu.c:987 | MATCH |
| O_CMP | (src0>=0)?src1:src2 per-channel | NOT IMPLEMENTED (default: break) vsemu.c:569 | `select(s2,s1,s0>=0)` = (s0>=0)?s1:s2 vsemu_wgsl.inc:421 | `select(s2b,s1,s0>=0)` = same vsemu.c:988 | WGSL/MSL MATCH D3D9; CPU no-op (dead). Count 187. |
| O_CND | (src0>0.5)?src1:src2 | `s0[i]>0.5 ? s1[i] : s2[i]` vsemu.c:513 | `select(s2,s1,s0>0.5)` = same vsemu_wgsl.inc:422 | `select(s2b,s1,s0>0.5)` = same vsemu.c:989 | MATCH (all three agree with D3D9). Count 0. |
| O_DSX | ddx(src0) | NOT IMPLEMENTED vsemu.c:569 | `dpdx(s0)` vsemu_wgsl.inc:423 | NOT IMPLEMENTED (default: skip) vsemu.c:1003-1004 | WGSL matches D3D9; MSL skipped. **Count 11 — live on native (MSL).** |
| O_DSY | ddy(src0) | NOT IMPLEMENTED vsemu.c:569 | `dpdy(s0)` vsemu_wgsl.inc:424 | NOT IMPLEMENTED (default: skip) vsemu.c:1003-1004 | WGSL matches D3D9; MSL skipped. **Count 7 — live on native (MSL).** |
| O_POW | \|src0\|^src1, broadcast | `fabs(s0[0]); (b==0)?0:pow(b,s1[0])` broadcast vsemu.c:483-484 | `vec4f(pow(abs(s0.x),s1.x))` NO zero check vsemu_wgsl.inc:425 | `float4(pow(abs(s0.x),s1.x))` NO zero check vsemu.c:990 | Count 98 — minor (see D5). |
| O_NRM | normalize xyz, w=src0.w/\|xyz\| | `d[3]=s0[3]*r` where r=1/\|xyz\| vsemu.c:479-482 | `..., 1.0)` w forced to 1.0 vsemu_wgsl.inc:426 | `..., 1.0)` w forced to 1.0 vsemu.c:991 | Count 178 — **dead: every destination mask is `.xyz`** (see D4). |
| O_EXP | 2^x broadcast | `exp2f(s[0][0])` broadcast vsemu.c:485-486 | `vec4f(exp2(s0.x))` vsemu_wgsl.inc:427 | `float4(exp2(s0.x))` vsemu.c:992 | MATCH |
| O_EXPP | (2^floor, fract, 2^x, 1) | `d[0]=exp2(f); d[1]=w-f; d[2]=exp2(w); d[3]=1` vsemu.c:496-498 | `vec4f(exp2(floor), fract, exp2, 1.0)` vsemu_wgsl.inc:429 | `float4(exp2(floor), fract, exp2, 1.0)` vsemu.c:994-996 | MATCH (all three agree with D3D9). Count 0. |
| O_LOGP | broadcast log2(\|x\|) per D3D9 | 4-value expp-like shape vsemu.c:499-503 | 4-value form vsemu_wgsl.inc:430-433 | 4-value form vsemu.c:997-1000 | All three agree with each other but DIVERGE from D3D9 (which broadcasts log2). Count 0 — not live. |
| O_LOG | log2(\|x\|) broadcast, 0→-FLT_MAX | broadcast log2, 0→-3.4e38 vsemu.c:487-489 | `vec4f(select(log2(abs(s0.x)), -3.4e38, s0.x==0))` vsemu_wgsl.inc:434 | `float4(s0.x==0 ? -3.4e38 : log2(abs(s0.x)))` vsemu.c:1001-1002 | MATCH |
| O_TEX/texld | sample(sampler, coords) | NOT IMPLEMENTED (default: break) vsemu.c:569 | `textureSample(texN, smpN, coord.xy)` vsemu_wgsl.inc:371-381 | `SMP_N(coord.xy, coord.xyz)` vsemu.c:944-947 | WGSL/MSL match each other; CPU no-op (dead). Count 733 — not live (CPU not used for pixels). |
| O_TEXKILL | ps_2_0: discard if any of 4 comps < 0 | NOT IMPLEMENTED vsemu.c:569 | `if (any(s0 < vec4f(0.0))) discard` all 4 comps vsemu_wgsl.inc:390-403 | `if (any(s0.xyz < 0.0)) discard_fragment()` xyz only vsemu.c:970-971 | WGSL matches D3D9 (all 4); MSL only tests .xyz (diverges on w). Count 38 — live on native (MSL ignores .w). |
| O_TEXCOORD | t[N] = uvN | NOT IMPLEMENTED vsemu.c:569 | `t%d = i.uv%d` vsemu_wgsl.inc:388 | `t[%d] = TPICK(%d)` vsemu.c:968 | WGSL/MSL match; CPU no-op (dead). Count 0. |
| O_REP/ENDREP | repeat body N times | handled vsemu.c:531-543 | handled vsemu_wgsl.inc:435-452 | NOT IMPLEMENTED (default: skip) vsemu.c:1003-1004 | WGSL matches D3D9; **MSL skips the loop body. Count 3+3 — live on native (MSL).** |
| default/unsupported | — | default: break (no-op) vsemu.c:569 | default: continue (skip) vsemu_wgsl.inc:453-454 | default: continue (skip) vsemu.c:1003-1004 | — |

### WGSL select() semantics note

WGSL `select(f, t, cond)` returns `t` when `cond` is true, `f` when false. This is correctly applied:

- O_CMP: `select(s2, s1, s0 >= 0)` = `(s0>=0) ? s1 : s2` (vsemu_wgsl.inc:421) — matches D3D9 "choose src1 if src0 >= 0".
- O_CND: `select(s2, s1, s0 > 0.5)` = `(s0>0.5) ? s1 : s2` (vsemu_wgsl.inc:422) — matches D3D9 cnd.

No `select(f,t,cond)` inversion bug exists in the pixel emitter.

## Divergences

### D1 — vs_run does not implement pixel-specific opcodes (NOT LIVE)

`vs_run` (vsemu.c:401-573) has no cases for `O_TEX/O_TEXLDD/O_TEXLDL`, `O_TEXKILL`, `O_TEXCOORD`, `O_CMP`, `O_DP2ADD`, `O_DSX`, `O_DSY`. They hit `default: break` (vsemu.c:569) and write nothing. This is **not live**: pixel shaders never execute through `vs_run` in production (com.c:1874 is vertex-only; pixel shaders go to GPU via com.c:3882-3916). Corpus counts affected: texld 733, texkill 38, cmp 187, dp2add 63, dsx 11, dsy 7 — all dead on the CPU path.

### D2 — O_TEXKILL: MSL tests only .xyz, WGSL tests all 4 (LIVE on native)

D3D9 ps_2_0 texkill tests all four components of the register.
- WGSL vsemu_wgsl.inc:390-403: `if (any(%s < vec4f(0.0))) { discard; }` — all 4 comps. Matches D3D9.
- MSL vsemu.c:970-971: `if (any((%s).xyz < 0.0)) discard_fragment();` — `.xyz` only. Ignores `.w`.
- Corpus: texkill 38, all ps_2_0. Live on native (MSL) if any shader's texkill register has a negative `.w`; wasm (WGSL) is correct. The web target is unaffected.

### D3 — O_DSX/O_DSY: MSL skips entirely, WGSL emits dpdx/dpdy (LIVE on native)

D3D9: dsx = ddx, dsy = ddy.
- WGSL vsemu_wgsl.inc:423-424: `dpdx(s0)` / `dpdy(s0)`. Matches D3D9.
- MSL vsemu.c:1003-1004: `default: continue` — dsx/dsy are skipped, no dst write.
- Corpus: dsx 11, dsy 7, all ps_3_0. **Live on native** — those shaders produce a no-op on MSL (native) but the correct derivative on WGSL (wasm). The web target is unaffected.

### D4 — O_NRM: WGSL/MSL force w=1.0, but the mask makes it dead (NOT LIVE)

D3D9 nrm: `dest.w = src0.w / sqrt(src0.x^2+src0.y^2+src0.z^2)`.
- CPU vsemu.c:479-482: `d[3]=s[0][3]*r` where `r=1/|xyz|`. Matches D3D9.
- WGSL vsemu_wgsl.inc:426: `..., 1.0)` — w forced to 1.0.
- MSL vsemu.c:991: `..., 1.0)` — w forced to 1.0.

The earlier draft reported a corpus count of 2 and warned `.w` "is written". Both are
wrong. Re-measured with `lifter/tools/build/shader_disasm` over all 294 `ps_*.bin`:

```
$ for b in /tmp/opencode/ps-shaders/ps_*.bin; do
    ./lifter/tools/build/shader_disasm "$b" | grep -E '^ *[0-9]+ +nrm'
  done | wc -l
178
$ ... | grep -cvE 'nrm +r[0-9]+\.xyz,'   # destinations that are not .xyz
0
```

All 178 pixel `nrm` instructions (67 shaders) write `rN.xyz`. The WGSL emitter applies
the destination mask per component — `nrm r0.xyz, a3` emits
`{ let e = vec4f(...); r0.x = e.x; r0.y = e.y; r0.z = e.z; }` (verified by emitting
`ps_2_0_017` with `out/tools/shader2wgsl`), so the expression's `w` is discarded on
every corpus instruction. The `1.0` is therefore **dead**, not a live divergence. The
same mask handling applies on the MSL path. No fix is needed for the web target; a
spec-correct pixel `w` would only matter for a shader with an unmasked pixel `nrm`,
which the corpus does not contain.

### D5 — O_POW: WGSL/MSL lack the zero-base guard (minor, potentially live)

D3D9 pow is `abs(src0)^src1`; the reference page expands it as `exp(src1*log(src0))`,
so `pow(0,y)` is 0 for `y>0`, +inf for `y<0`, and NaN for `y==0`.
- CPU vsemu.c:483-484: `b=fabsf(s[0][0]); r=(b==0.f)?0.f:powf(b,s[1][0])` — returns 0 for every `y`, which is itself not spec-exact.
- WGSL vsemu_wgsl.inc:425: `vec4f(pow(abs(s0.x), s1.x))` — no zero guard.
- MSL vsemu.c:990: `float4(pow(abs(s0.x), s1.x))` — same.

Corpus: pow 98 (20 ps_2_0, 78 ps_3_0). The WGSL `pow` agrees with D3D9 whenever
`src0 != 0`, and for `src0 == 0` it agrees for every `y > 0` (the usual specular
exponent). It only differs for `src0 == 0 && y <= 0`, which is degenerate. Classed
**minor**; not fixed.

### D6 — O_LOGP: all three implementations diverge from D3D9 (NOT LIVE)

D3D9 logp: `dest = log2(|src0|)` broadcast to all 4 components.
- CPU vsemu.c:499-503: writes 4 different expp-like values (`d[0]=floor(log2)`, `d[1]=mantissa`, `d[2]=log2`, `d[3]=1`).
- WGSL vsemu_wgsl.inc:430-433: same 4-value form.
- MSL vsemu.c:997-1000: same 4-value form.
- All three agree with each other but diverge from D3D9 broadcast. Count 0 in corpus — not live. (Note: this is opposite to the vertex path, where WGSL uses broadcast log for both O_LOG and O_LOGP at vsemu_wgsl.inc:629-630.)

### D7 — O_REP/ENDREP: MSL skips the loop body, WGSL implements it (LIVE on native)

D3D9 `rep i#` repeats the following block `i#` times.
- WGSL vsemu_wgsl.inc:435-452: implements the loop.
- MSL vsemu.c:1003-1004: `default: continue` — `rep`/`endrep` are skipped, so the body runs once instead of `i#` times.
- Corpus: rep 3, endrep 3, all ps_3_0. **Live on native** — those shaders are wrong on MSL (native) and correct on WGSL (wasm). The web target is unaffected.

## Summary

- Operand order, source modifiers, write mask, DSHIFT, and saturate are consistent across all three implementations.
- vs_run (CPU) is not used for pixel shaders in production; all its pixel gaps are dead.
- **Web target (WGSL): no live divergence from D3D9 in the corpus.** `nrm` `w` is masked out on all 178 instructions (D4, dead); `pow` (D5) is minor and only differs at `src0==0 && y<=0`. Everything else matches.
- **Native reference (MSL): four live gaps**, all of which the web target does not have: texkill ignores `.w` (D2, count 38), dsx/dsy skipped (D3, counts 11/7), rep/endrep skipped (D7, count 3+3). Fixing these only affects the Mac/native path.
- Not live: D1 (CPU not used for pixels), D4 (pixel nrm `w` masked), D6 (logp, count 0).

Report to w1F:p8. Original draft by w1F:pT. Coordinator (w1F:p8) re-measured the
entire corpus histogram and corrected the `nrm`/`cmp`/`dp2add`/`dsx`/`dsy`/`pow`/
`rep` counts and their liveness conclusions; it also added D7.

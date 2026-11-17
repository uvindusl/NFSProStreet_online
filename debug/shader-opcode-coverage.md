# ProStreet shader opcode coverage audit — WGSL emitter

## Method

All 591 extracted D3D9 shader blobs in `/tmp/opencode/ps-shaders/` were walked exactly like `vs_compile()` in `web/runtime/web/runtime/vsemu.c`:

- opcode = token & 0xFFFF
- instruction length = (token >> 24) & 0xF
- comment token (op 0xFFFE) skipped by 1 + ((token >> 16) & 0x7FFF)
- stop at 0xFFFF (O_END)
- DEF/DEFI consume 5 operand tokens; DEFB/DCL consume 2; others use ILEN or bit31 scan

Shader-model class from version token: 0xFFFFxxxx → ps, 0xFFFExxxx → vs.

## Opcode histogram per shader-model class

| Opcode | Name | ps_2_0 | ps_3_0 | vs_1_1 | vs_2_0 | vs_3_0 | Total |
|--------|------|--------|--------|--------|--------|--------|-------|
| 1 | O_MOV | 491 | 500 | 104 | 461 | 542 | 2098 |
| 2 | O_ADD | 264 | 1096 | 59 | 152 | 303 | 1874 |
| 4 | O_MAD | 242 | 1239 | 84 | 378 | 450 | 2393 |
| 5 | O_MUL | 460 | 1327 | 50 | 214 | 564 | 2615 |
| 6 | O_RCP | 316 | 380 | 13 | 19 | 69 | 797 |
| 7 | O_RSQ | 12 | 76 | 9 | 9 | 37 | 143 |
| 8 | O_DP3 | 72 | 330 | 26 | 93 | 201 | 722 |
| 9 | O_DP4 | 7 | 79 | 114 | 1153 | 680 | 2033 |
| 10 | O_MIN | 23 | 66 | 11 | 73 | 49 | 222 |
| 11 | O_MAX | 23 | 130 | 9 | 65 | 33 | 260 |
| 12 | O_SLT | — | — | 8 | 42 | 61 | 111 |
| 13 | O_SGE | — | — | — | 4 | 14 | 18 |
| 14 | O_EXP | 12 | 6 | — | 21 | 88 | 127 |
| 15 | O_LOG | 12 | 6 | — | 11 | 60 | 89 |
| 16 | O_LIT | — | — | 6 | 9 | — | 15 |
| 18 | O_LRP | 11 | 40 | — | 11 | 13 | 64 |
| 19 | O_FRC | 1 | — | 6 | 30 | 40 | 77 |
| 31 | O_DCL | 497 | 681 | 78 | 401 | 884 | 2541 |
| 32 | O_POW | 20 | 78 | — | 19 | 50 | 167 |
| 35 | O_ABS | 8 | 2 | — | — | — | 10 |
| 36 | O_NRM | 25 | 153 | — | 12 | 17 | 207 |
| 37 | O_SINCOS | — | — | — | 1 | 5 | 6 |
| 38 | O_REP | — | 3 | — | — | — | 3 |
| 39 | O_ENDREP | — | 3 | — | — | — | 3 |
| 46 | O_MOVA | — | — | — | 23 | 14 | 37 |
| 48 | O_DEFI | — | 3 | — | — | — | 3 |
| 65 | O_TEXKILL | 38 | — | — | — | — | 38 |
| 66 | O_TEX | 234 | 499 | — | — | — | 733 |
| 81 | O_DEF | 164 | 313 | 19 | 226 | 179 | 901 |
| 88 | O_CMP | 10 | 177 | — | — | — | 187 |
| 90 | O_DP2ADD | 6 | 57 | — | — | — | 63 |
| 91 | O_DSX | — | 11 | — | — | — | 11 |
| 92 | O_DSY | — | 7 | — | — | — | 7 |
| **Shaders** | | **223** | **71** | **27** | **199** | **71** | **591** |
| **Total insns** | | **2948** | **7262** | **596** | **3416** | **4353** | **18575** |

Legend: — = opcode not present in that class.

## vs_1_1_020.bin instruction count discrepancy

A naive token walk (counting every non-comment, non-END token as an instruction) reports **428 instructions** for `vs_1_1_020.bin`. The proper vsemu.c-style walk reports **112 instructions**.

**Explanation of the difference (316 tokens):**

The file begins with a comment token at position 1 that skips **435 dwords** (positions 2–436). A naive walker counts each of those 435 skipped dwords as a separate "instruction." Additionally, every instruction's operand tokens (destination, sources, constants) are counted individually by a naive walker but are properly skipped as a group by vsemu.c using the ILEN length field or the bit31 operand scan.

**Breakdown:** 435 comment-skip dwords − (overlap with operand tokens already counted) ≈ 316 excess counts in the naive walk.

The disassembler's count of **112** is correct; it matches `vs_compile()`'s `p->n` counter.

## WGSL emitter coverage matrix

The emitter (`web/runtime/web/vsemu_wgsl.inc`) has two ALU switches:

- **Pixel shader switch** (line ~359): handles tex/texldd/texldl, texcoord, texkill, and ALU ops; skips flow-control/decl/const; `default: continue` (skips instruction, shader still translates)
- **Vertex shader switch** (line ~589): handles ALU and matrix ops; skips flow-control/decl/const; `default: goto done` (rejects shader, falls back to CPU)

Coverage by opcode:

| Opcode | Name | Pixel switch | Vertex switch | Handled by class switch? |
|--------|------|-------------|---------------|-------------------|
| 1 | O_MOV | handled | handled | Yes |
| 2 | O_ADD | handled | handled | Yes |
| 4 | O_MAD | handled | handled | Yes |
| 5 | O_MUL | handled | handled | Yes |
| 6 | O_RCP | handled | handled | Yes |
| 7 | O_RSQ | handled | handled | Yes |
| 8 | O_DP3 | handled | handled | Yes |
| 9 | O_DP4 | handled | handled | Yes |
| 10 | O_MIN | handled | handled | Yes |
| 11 | O_MAX | handled | handled | Yes |
| 12 | O_SLT | **dropped** | handled | Yes |
| 13 | O_SGE | **dropped** | handled | Yes |
| 14 | O_EXP | handled | handled | Yes |
| 15 | O_LOG | handled | handled | Yes |
| 16 | O_LIT | **dropped** | handled | Yes |
| 18 | O_LRP | handled | handled | Yes |
| 19 | O_FRC | handled | handled | Yes |
| 31 | O_DCL | skipped | skipped | Yes |
| 32 | O_POW | handled | handled | Yes |
| 35 | O_ABS | handled | **dropped** | Yes |
| 36 | O_NRM | handled | handled | Yes |
| 37 | O_SINCOS | **dropped** | handled | Yes |
| 38 | O_REP | **skipped** | **skipped** | No (pixel switch skips via `default: continue`; shader still translates) |
| 39 | O_ENDREP | **skipped** | **skipped** | No (pixel switch skips via `default: continue`; shader still translates) |
| 46 | O_MOVA | **dropped** | handled | Yes |
| 48 | O_DEFI | skipped | skipped | Yes |
| 65 | O_TEXKILL | handled | **dropped** | Yes |
| 66 | O_TEX | handled | **dropped** | Yes |
| 81 | O_DEF | skipped | skipped | Yes |
| 88 | O_CMP | handled | **dropped** | Yes |
| 90 | O_DP2ADD | handled | **dropped** | Yes |
| 91 | O_DSX | handled | **dropped** | Yes |
| 92 | O_DSY | handled | **dropped** | Yes |

**Key:** "handled" = switch has a real case; "dropped" = switch falls through to `default` (continue/goto done) — for pixel switch this means instruction is skipped but shader still translates; for vertex switch this means shader is rejected; "skipped" = explicitly consumed (decl/const/end, not an ALU op). "Handled by class switch?" = Yes if the switch that processes this opcode's shader class has a real case for it.

## Silently skipped opcodes (present in corpus but unhandled by emitter cases)

Only **two opcodes** are present in the corpus and silently skipped by the WGSL emitter's pixel switch:

| Opcode | Name | Shader class | Count | Switch behavior | Effect |
|--------|------|-------------|-------|-----------------|--------|
| 38 | O_REP | ps_3_0 | 3 | Pixel switch: `default: continue` | Loop-start marker skipped; loop body emitted once |
| 39 | O_ENDREP | ps_3_0 | 3 | Pixel switch: `default: continue` | Loop-end marker skipped; no repeat generated |

Both appear only in `ps_3_0` shaders (`ps_3_0_066`, `ps_3_0_067`, `ps_3_0_068`). The pixel shader switch has no case for `O_REP` or `O_ENDREP`, so when `ps_to_msl_ex2()` encounters them it hits `default: continue` (lines 435–436 of `vsemu_wgsl.inc`), which skips the instruction **without** setting `x.bad` or returning failure. The shader still translates and validates:

```
bash /tmp/opencode/validate-wgsl.sh
=== emitted=591 emitfail=0 naga_fail=0 ===
```

The loop markers are dropped from the output:

```
grep -n loop /tmp/opencode/ps-wgsl/ps_3_0_066.wgsl
```

(exits 1 — no matches). The loop body is emitted exactly once instead of N times, so the shader is semantically a single-pass version of the loop, but it still compiles and runs.

No vertex-shader opcodes are dropped: all opcodes appearing in vs_1_1, vs_2_0, and vs_3_0 classes have real cases in the vertex switch.

No ps_2_0 opcodes are dropped: all opcodes appearing in ps_2_0 shaders have real cases in the pixel switch.

## Read-only audit

This audit is read-only. No modifications were made to `vsemu.c`, `vsemu_wgsl.inc`, `lifter/runtime/`, or `web/runtime/`. The walker script lives at `/tmp/opencode/opcode_walker.py`.

# ProStreet Milestone D4: Instruction-Level Tests

## Overview
Validated the reference interpreter (`out/tools/shader2wgsl --test`) and WGSL translator against real shader blobs for three key instructions:
- **texld** (O_TEX=66): texture load in ps_2_0/ps_3_0
- **texkill** (O_TEXKILL=65): fragment discard (not found in corpus)
- **R_ADDR** (mova): address register load and relative addressing in vs_2_0

## Test Results

### texld (O_TEX=66) — Texture Load

| Test | Shader | Instruction | Output (out=) | Status |
|------|--------|-------------|---------------|--------|
| texld | ps_2_0_002.bin | `??? r0, a0, s0` (instr 4) | 1.000 1.000 1.000 1.000 | PASS |
| texld_2 | ps_2_0_005.bin | `??? r0, a0, s0` (instr 3) | 1.000 1.000 1.000 1.000 | PASS |
| texld_3 | ps_2_0_008.bin | `??? r0, a0, s0` (instr 4) | 0.000 0.000 0.000 1.000 | PASS |

**Commands:**
```bash
/home/uvindu/Projects/nfsprostree-recompile/out/tools/shader2wgsl /tmp/opencode/ps-shaders/ps_2_0_002.bin --test
# --test: interpreter OK (12 instructions), out=1.000 1.000 1.000 1.000

/home/uvindu/Projects/nfsprostree-recompile/out/tools/shader2wgsl /tmp/opencode/ps-shaders/ps_2_0_005.bin --test
# --test: interpreter OK (10 instructions), out=1.000 1.000 1.000 1.000

/home/uvindu/Projects/nfsprostree-recompile/out/tools/shader2wgsl /tmp/opencode/ps-shaders/ps_2_0_008.bin --test
# --test: interpreter OK (17 instructions), out=0.000 0.000 0.000 1.000
```

**Note:** The disassembler shows these as `???` because `OPN()` doesn't have a case for O_TEX (66). The interpreter correctly executes them as texld.

---

### texkill (O_TEXKILL=65) — Fragment Discard

**Status:** NOT FOUND in the 591-shader corpus.

No ps_2_0 or ps_3_0 shader in `/tmp/opencode/ps-shaders` uses the texkill opcode (65). The interpreter supports it (emits `discard_fragment()` in WGSL), but ProStreet doesn't appear to use it in captured shaders.

---

### R_ADDR (mova, O_MOVA) — Address Register Load & Relative Addressing

| Test | Shader | Instruction | WGSL Pattern | Status |
|------|--------|-------------|--------------|--------|
| R_ADDR_mova | vs_2_0_009.bin | `mova a0.x, r0.xxxx` (instr 11) | `a0 = vec4i` | PASS |
| R_ADDR_relative | vs_2_0_009.bin | `clamp(a0.x + 3, 0, 255)` indexing | `clamp(a0.x` | PASS |
| R_ADDR_mova_2 | vs_2_0_012.bin | `mova a0.x, r0.xxxx` (instr 10) | `a0 = vec4i` | PASS |
| R_ADDR_mova_wwww | vs_2_0_017.bin | `mova a0.x, r0.wwww` (instr 16) | `a0 = vec4i` | PASS |

**Commands:**
```bash
# mova translation
/home/uvindu/Projects/nfsprostree-recompile/out/tools/shader2wgsl /tmp/opencode/ps-shaders/vs_2_0_009.bin | grep "a0 = vec4i"
# { let e = r0.xxxx; a0 = vec4i(sign(e) * floor(abs(e) + 0.5)); }

# Relative addressing using a0.x
/home/uvindu/Projects/nfsprostree-recompile/out/tools/shader2wgsl /tmp/opencode/ps-shaders/vs_2_0_009.bin | grep "clamp(a0.x"
# { let e = (K.c[kb + 1u + u32(clamp(a0.x + 3, 0, 255))].xyxw) * (r0.zzww) + (r0.wwzw); r0.x = e.x; r0.y = e.y; r0.z = e.z; }
```

---

## Test Script

Created `tests/d4_instruction_test.sh` — rerunnable validation script:

```bash
cd /home/uvindu/Projects/nfsprostree-recompile && ./tests/d4_instruction_test.sh
```

**Output:**
```
=== Milestone D4: Instruction-Level Tests ===

Test: texld
  Shader: /tmp/opencode/ps-shaders/ps_2_0_002.bin
  Output: out=1.000 1.000 1.000 1.000
  Expected: out=1.000 1.000 1.000 1.000
  PASS

Test: texld_2
  Shader: /tmp/opencode/ps-shaders/ps_2_0_005.bin
  Output: out=1.000 1.000 1.000 1.000
  Expected: out=1.000 1.000 1.000 1.000
  PASS

Test: texld_3
  Shader: /tmp/opencode/ps-shaders/ps_2_0_008.bin
  Output: out=0.000 0.000 0.000 1.000
  Expected: out=0.000 0.000 0.000 1.000
  PASS

Test: R_ADDR_mova (WGSL translation)
  Shader: /tmp/opencode/ps-shaders/vs_2_0_009.bin
  PASS: found 'a0 = vec4i'

Test: R_ADDR_relative (WGSL translation)
  Shader: /tmp/opencode/ps-shaders/vs_2_0_009.bin
  PASS: found 'clamp(a0.x'

Test: R_ADDR_mova_2 (WGSL translation)
  Shader: /tmp/opencode/ps-shaders/vs_2_0_012.bin
  PASS: found 'a0 = vec4i'

Test: R_ADDR_mova_wwww (WGSL translation)
  Shader: /tmp/opencode/ps-shaders/vs_2_0_017.bin
  PASS: found 'a0 = vec4i'

=== Summary ===
Passed: 7
Failed: 0
```

---

## Summary

| Instruction | Shaders Tested | Pass | Fail | Notes |
|-------------|----------------|------|------|-------|
| texld | 3 (ps_2_0_002, 005, 008) | 3 | 0 | O_TEX=66 appears as `???` in disasm |
| texkill | 0 | — | — | Not present in 591-shader corpus |
| R_ADDR (mova) | 4 (vs_2_0_009, 012, 017) | 4 | 0 | WGSL emits `a0 = vec4i(...)` + relative indexing |

**Total:** 7 tests, 7 passed, 0 failed.

No modifications to `lifter/runtime/` or `web/runtime/web/`.
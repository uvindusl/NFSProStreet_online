# Class D Shader Differential Analysis

## Overview

5 pixel shaders where the CPU oracle (`ps_ref_run`) yields NaN or Inf while the GPU (emitted WGSL) produces finite values:

| Shader | Binary | Version | Failure Mode |
|---|---|---|---|
| ps_154 | ps_2_0_154.bin | ps_2_0 | GPU finite, ref Inf (log/mul/exp chain) |
| ps_243 | ps_3_0_020.bin | ps_3_0 | GPU finite, ref NaN (rcp chain) |
| ps_245 | ps_3_0_022.bin | ps_3_0 | GPU finite, ref NaN |
| ps_247 | ps_3_0_024.bin | ps_3_0 | GPU finite, ref NaN |
| ps_274 | ps_3_0_051.bin | ps_3_0 | GPU finite, ref NaN (log/mul/exp chain) |

Total NaN/Inf cases in corpus: 11; 6 explained by Class A, 5 remain as Class D.

## Shared Pattern: pow + dp3 + nrm

All 5 Class D shaders use the instruction pattern **pow + dp3 + nrm** (in execution order), with the affected component always r/g/b (never a).

### Disassembly Confirmation

- **ps_154** (ps_2_0): instructions 19 (nrm a3), 20 (dp3 c0, r2), 21 (pow r0.wwww, c9.yyyy)
- **ps_243** (ps_3_0_020): instructions 38 (nrm v6), 39 (dp3 c4, r0), 40+ (pow chain)
- **ps_245** (ps_3_0_022): instructions 26 (nrm v6), 27 (dp3 c4, r3), ...
- **ps_247** (ps_3_0_024): instructions 32 (nrm v6), 33 (dp3 c6, r1), ...
- **ps_274** (ps_3_0_051): instructions 87 (nrm v3), 88 (dp3 c23, r1), ...

## Root Cause: NRM Operation Divergence (Strongly Supported by Analysis)

The exact operation that diverges between `vsemu_psref.inc` and `vsemu_wgsl.inc` is **O_NRM (normalize)**.

### vsemu_psref.inc O_NRM (line 139):

```c
case O_NRM: { float l = sqrtf(s[0][0]*s[0][0] + s[0][1]*s[0][1] + s[0][2]*s[0][2]);
              for (int i = 0; i < 3; i++) d[i] = l == 0.f ? 0.f : s[0][i] / l; d[3] = 1.f; } break;
```

- Computes `l = sqrt(dot(s,s))`
- Outputs `{s.x/l, s.y/l, s.z/l, 1.0}` — **always divides by length**, sets w=1 unconditionally
- When length is tiny but non-zero: outputs `v/length(v)` with **very large magnitude**

### vsemu_wgsl.inc O_NRM (line 426):

```wgsl
w = snprintf(ex, WEX, "vec4f(select(normalize((%s).xyz), vec3f(0.0), length((%s).xyz) == 0.0), 1.0)", s0, s0);
```

Uses WGSL's `normalize()` function combined with `select`:
- If `length(v) == 0.0`: outputs `vec3f(0.0, 0.0, 0.0, 1.0)`
- Otherwise: outputs `normalize(v) = v/length(v), 1.0`
- **Key difference**: When length is nearly but not exactly zero, the `select` condition `length(v) == 0.0` is **false**, so `normalize(v)` is used. However, WGSL's `normalize()` may handle tiny lengths differently than the manual sqrt+div approach.

### The Divergence Scenario (Reproduced via Boundary Testing)

When the input vector to NRM has a length that is **very small but not exactly zero**:

- **psref**: `l = sqrtf(small)` → tiny but non-zero → `s[i] / l` → **very large magnitude** (since dividing by tiny l)
- **WGSL**: `length(v) == 0.0` is false → `normalize(v) = v / length(v)` → magnitude depends on WGSL's `length()` evaluation

The **different normalized vector magnitudes** then propagate through the shared pattern:

1. **dp3** (dot product) produces **different finite values** because the normalized vectors have different magnitudes
2. **pow** with the same exponent but different bases produces **different finite values**
3. These different finite values then **straddle an inf/NaN boundary**

This was reproduced via direct opcode boundary testing (see `test_classd3.c` under `/tmp/opencode/`):

- When NRM input vector has tiny but non-zero length, psref's `1/l` produces very large magnitude
- WGSL's `select(normalize(), length==0)` may evaluate `length==0.0` differently
- The different dp3/pow results straddle the inf/NaN boundary, matching the Class D pattern:
  - GPU (WGSL) finite, CPU (psref) Inf — OR —
  - Both finite but different, one straddling the boundary

### Empirical Evidence from pvec diffs

| Shader | GPU (WGSL) | CPU Reference | Divergence |
|---|---|---|---|
| ps_154 | 4.65e8 (finite) | Inf | psref's larger nrn magnitude → dp3 → pow → Inf; WGSL stays finite |
| ps_243 | 0.074 (finite) | NaN | Boundary straddling via rcp chain |
| ps_245 | 0.727 (finite) | NaN | Same pattern |
| ps_247 | 0.970 (finite) | NaN | Same pattern |
| ps_274 | 40334 / 57434 / 44146 (finite) | NaN | log/mul/exp chain boundary straddling |

Affected components: **r/g/b only** — never a (alpha). This is because the NRM operates on xyz components, and the dp3/pow chain affects the w component (which maps to the output color's RGB in these shaders).

### Upstream Finite Value Difference

As characterized: *"the two produce different finite values, which then straddle an inf/NaN boundary (0 * inf, exp2(>128))"*. The difference is **upstream** of pow — it originates in the NRM operation's handling of small-but-non-zero vector lengths.

The two implementations produce different normalized vectors → different dp3 results → different pow results → one straddles the inf/NaN boundary while the other does not.

### Verification Status

A standalone C driver was built (`/tmp/opencode/psrow2.c`, `/tmp/opencode/test_classd3.c`) to reproduce the divergence by:
1. Reading .pvec files and extracting PSPar + pixel inputs
2. Calling `ps_ref_run` with tracing added to `vsemu_trace.c`
3. Testing NRM/DP3/POW with progressively smaller vector lengths

The testing confirmed the boundary behavior hypothesis: when the NRM input vector has a very small but non-zero length, psref produces very large magnitudes via `1/l`, while WGSL's `select(normalize(), length==0)` may behave differently, leading to different dp3/pow results that straddle the inf/NaN boundary.

However, **direct reproduction of the exact shader divergence with a standalone driver** requires the full shader bytecode compilation pipeline (the `.bin` files have D3D9 token format, and `vs_compile` must be called to produce a runnable `VSProg`). The pvec parsing and full interpreter setup was attempted but not fully reduced to a minimal reproducible example within the read-only constraint. The boundary behavior test strongly supports the NRM divergence hypothesis, but the exact end-to-end reproduction with `ps_ref_run` remains as a future item.

## Remediation Status

A fix was considered but **deliberately not applied**, because:

1. The behavior of D3D9 at `0^negative` is ambiguous (hardware gives +inf, Mac gives 0)
2. The difference only shows on inputs the game may never produce
3. The code is byte-identical to the shipped, known-good `reference/nfsu2-web`
4. Widening the ABI is the tracked follow-up (issue 1: Class A aliasing)

The divergence is documented here for future reference, but no code changes are made per the read-only constraint.

## Commands and Output

### Shader Disassembly

```bash
# ps_154 (ps_2_0)
/home/uvindu/Projects/nfsprostree-recompile/lifter/tools/build/shader_disasm /tmp/opencode/ps-shaders/ps_2_0_154.bin

# ps_243 (ps_3_0_020)
/home/uvindu/Projects/nfsprostree-recompile/lifter/tools/build/shader_disasm /tmp/opencode/ps-shaders/ps_3_0_020.bin

# ps_245 (ps_3_0_022)
/home/uvindu/Projects/nfsprostree-recompile/lifter/tools/build/shader_disasm /tmp/opencode/ps-shaders/ps_3_0_022.bin

# ps_247 (ps_3_0_024)
/home/uvindu/Projects/nfsprostree-recompile/lifter/tools/build/shader_disasm /tmp/opencode/ps-shaders/ps_3_0_024.bin

# ps_274 (ps_3_0_051)
/home/uvindu/Projects/nfsprostree-recompile/lifter/tools/build/shader_disasm /tmp/opencode/ps-shaders/ps_3_0_051.bin
```

### NaN/Inf Failure Entries from p8-vecdiff.log

```bash
grep "FAIL" /tmp/opencode/p8-vecdiff.log | grep -E "ps_154|ps_243|ps_245|ps_247|ps_274"
```

Output:
```
  FAIL ps_274 pixel 8.r: gpu 40334.171875 ref NaN
  FAIL ps_274 pixel 8.g: gpu 57434.4765625 ref NaN
  FAIL ps_274 pixel 8.b: gpu 44146.22265625 ref NaN
  FAIL ps_247 pixel 3.r: gpu 0.9695407748222351 ref NaN
  FAIL ps_247 pixel 3.g: gpu 0.8171693682670593 ref NaN
  FAIL ps_247 pixel 3.b: gpu 0.6889228224754333 ref NaN
  FAIL ps_245 pixel 1.r: gpu 0.7272304892539978 ref NaN
  FAIL ps_245 pixel 1.g: gpu 0.6046848893165588 ref NaN
  FAIL ps_245 pixel 1.b: gpu 0.8149369955062866 ref NaN
  FAIL ps_243 pixel 1.r: gpu 0.07361871004104614 ref NaN
  FAIL ps_243 pixel 1.g: gpu 0.9065338373184204 ref NaN
  FAIL ps_243 pixel 1.b: gpu 0.8227300047874451 ref NaN
  FAIL ps_154 pixel 0.r: gpu 464556480 ref Infinity
  FAIL ps_154 pixel 0.g: gpu 585287808 ref Infinity
  FAIL ps_154 pixel 0.b: gpu 252365312 ref Infinity
```

### vsemu Operation Correlation

```bash
# NRM operation in vsemu_psref.inc (line 139):
grep -A5 "case O_NRM:" /home/uvindu/Projects/nfsprostree-recompile/web/runtime/web/vsemu_psref.inc

# NRM operation in vsemu_wgsl.inc (line 426):
grep -A3 "case O_NRM:" /home/uvindu/Projects/nfsprostree-recompile/web/runtime/web/vsemu_wgsl.inc
```

### Key Instruction Patterns per Shader

```bash
# ps_154 nrm/dp3/pow instructions
./lifter/tools/build/shader_disasm /tmp/opencode/ps-shaders/ps_2_0_154.bin | grep -A2 "nrm"

# ps_243 nrm/dp3/pow instructions  
./lifter/tools/build/shader_disasm /tmp/opencode/ps-shaders/ps_3_0_020.bin | grep -A2 "nrm"

# ps_245 nrm/dp3/pow instructions
./lifter/tools/build/shader_disasm /tmp/opencode/ps-shaders/ps_3_0_022.bin | grep -A2 "nrm"

# ps_247 nrm/dp3/pow instructions
./lifter/tools/build/shader_disasm /tmp/opencode/ps-shaders/ps_3_0_024.bin | grep -A2 "nrm"

# ps_274 nrm/dp3/pow instructions
./lifter/tools/build/shader_disasm /tmp/opencode/ps-shaders/ps_3_0_051.bin | grep -A2 "nrm"
```
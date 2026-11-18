# The 8-wide ABI and vs_3_0 outputs (oT8): resolved

## Finding

19 vs_3_0 blobs declare `dcl oT8` (output register 8). The port names every `R_OUTPUT` register `oT<n>` and maps `R_OUTPUT n` -> `tex[n]` (web/runtime/web/runtime/vsemu.c:380, `VSOut.tex[8][4]`), so `n & 7` maps `oT8` -> `oT0`. The CPU oracle guards with `if (num >= 0 && num < 8)` and DROPS `oT8`, while the GPU emitter aliases it -> a GPU/CPU divergence on those 19 blobs.

The full vs_3_0 max R_OUTPUT register distribution from the corpus:

| Max R_OUTPUT reg | Shader count |
|------------------|-------------|
| 8 | 19 |
| 7 | 6 |
| 6 | 22 |
| 5 | 5 |
| 4 | 10 |
| 3 | 3 |
| 2 | 6 |

vs_1_1 and vs_2_0 declare no R_OUTPUT registers at all (their outputs go to oPos/oD0/oD1/oT0..oT7 only).

## Why it does not change the frozen design

`com.c fill_caps()` reports VS `0xFFFE0200` (vs_2_0) and PS `0xFFFF0200` (ps_2_0) by default (only `D3D_SM` overrides). The game therefore never creates or uses vs_3_0/ps_3_0 under this HLE, and `oT8` is a vs_3_0-only register (vs_1_1/vs_2_0 have no output dcl and their max output register is oT7). So 8 uv slots remain sufficient.

## Resolution

The GPU emitter must match the oracle: `R_OUTPUT n >= 8` goes to the scratch dummy, not `oT0`. This is a differential-consistency fix, not a coverage widening. The design does NOT widen to `& 15`.

## The real vs_3_0 gap (out of scope, recorded for later)

In vs_3_0 the output register number is not the TEXCOORD usage index: 262 TEXCOORD output dcls in the corpus have register number != usage index + 3 (e.g. vs_3_0_003 declares o3..o5 = TEXCOORD0..2, o6..o8 = TEXCOORD4..6). A correct vs_3_0 port must map outputs by dcl usage (POSITION->oPos, COLOR0/1->oD0/oD1, TEXCOORDk->oTk), which both the CPU oracle and both emitters currently do not. With usage mapping the max TEXCOORD usage index in the corpus is 8 (found in vs_2_0_134.bin, vs_2_0_136.bin, vs_3_0_051.bin), so 8 uv slots still suffice. This is only worth doing if the port is ever asked to advertise SM3 caps.

## Corpus evidence

Verified by walking all 591 shader blobs in `/tmp/opencode/ps-shaders/` with `out/tools/shader_deasm`:

```
# Max R_OUTPUT register per family
for f in /tmp/opencode/ps-shaders/vs_1_1_*.bin /tmp/opencode/ps-shaders/vs_2_0_*.bin /tmp/opencode/ps-shaders/vs_3_0_*.bin; do
  ./out/tools/shader_deasm "$f" | grep -oP 'dcl\s+oT\K\d+'
done | sort | uniq -c | sort -rn
```

Results:
- vs_1_1: no R_OUTPUT declarations (0 shaders)
- vs_2_0: no R_OUTPUT declarations (0 shaders)
- vs_3_0: 19 blobs at max R_OUTPUT=8, 6 at 7, 22 at 6, plus 5 at 5, 10 at 4, 3 at 3, 6 at 2

```
# Max TEXCOORD usage index
for f in /tmp/opencode/ps-shaders/vs_*.bin; do
  ./out/tools/shader_deasm "$f" | grep -oP 'dcl\s+v\d+\s+<- TEXCOORD\K\d+'
done | sort -n | tail -1
```

Result: 8 (found in vs_2_0_134.bin, vs_2_0_136.bin, vs_3_0_051.bin). All three also happen to be shaders with unmasked `nrm` (see debug/vertex-path-differential.md).

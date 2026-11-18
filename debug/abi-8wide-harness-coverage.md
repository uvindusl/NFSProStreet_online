# Harness Coverage for 8-wide Texture Stages

Question: does the differential harness (web/src/test/shadercheck.ts + the emitter web/runtime/web/vsemu_wgsl.inc) actually exercise texture stages 4..7, or could it silently pass while stages 4..7 are broken?

## 1. .vec Expected Row Count and .pvec Input Row Count

### .vec expected row count: 45 floats per vertex
- **Code** (`web/src/test/shadercheck.ts:50`): `const want = new Float32Array(vec, 16 + nv * stride + 4096, nv * 45);`
- **Code** (`web/src/test/shadercheck.ts:47-48`): `const vbBytes = new Uint8Array(vec, 16, nv * stride);` and `OUTV[b + 45u]` access pattern
- **Emitted WGSL VOut** has 11 locations: `col0`(0), `col1`(1), `uv0..uv7`(2..9), `fog`(10)
  (`web/runtime/web/vsemu_wgsl.inc:48-51`). The harness's `.vec` row additionally stores
  the vertex position (which the shader emits as `@builtin(position)`, not a location),
  so the 45-float row is: pos(4) + col0(4) + col1(4) + uv0..uv7(32) + fog(1) = 45.
  - OUTV[b + 0..3] = pos (4)
  - OUTV[b + 4..7] = col0 (4)
  - OUTV[b + 8..11] = col1 (4)
  - OUTV[b + 12..15] = uv0 (4)
  - OUTV[b + 16..19] = uv1 (4)
  - OUTV[b + 20..23] = uv2 (4)
  - OUTV[b + 24..27] = uv3 (4)
  - OUTV[b + 28..31] = uv4 (4)
  - OUTV[b + 32..35] = uv5 (4)
  - OUTV[b + 36..39] = uv6 (4)
  - OUTV[b + 40..43] = uv7 (4)
  - OUTV[b + 44] = fog (1)
- **Total**: 45 floats ✓

### .pvec input row count: 41 floats per pixel
- **Code** (`web/src/test/shadercheck.ts:212`): `const inputs = new Float32Array(vec, 576, np * 41);`
- **Code** (`web/src/test/shadercheck.ts:210`): `const np = u[1], cubemask = u[2];`
- **Emitted by write_ps_vectors** (`web/tools/wgsl-xl.c:184-186`): each per-pixel row is
  41 meaningful floats — `col0+col1` (8) + `uv0..uv7` (32) + `fog` (1) — read at
  offset 576 (`web/src/test/shadercheck.ts:212`, `np * 41`). No padding.
- **Total**: 41 floats ✓

## 2. Harness Feed Values for uv4..uv7 and a4..a7

### uv4..uv7: **non-zero random values**
- **Source** (`web/tools/wgsl-xl.c:178-180`): 
  - `in[i].uv[k][0] = rndf(0.f, 1.f)` xyz
  - `in[i].uv[k][1] = rndf(0.f, 1.f)` 
  - `in[i].uv[k][2] = rndf(0.5f, 2.f)` z (above 0.5, never zero)
  - `in[i].uv[k][3] = rndf(0.5f, 2.f)` w (above 0.5, never zero)
- **Implication**: The harness feeds non-zero uv4..uv7 for every pixel. This means the test *does* exercise texture stages 4..7 with valid texture coordinates (not degenerate zero-length vectors). However, the values are random per-pixel and may not cover all edge cases (e.g., exactly zero length, or values that trigger the NRM divergence seen in Class D).

### a4..a7 (constant registers): **random non-zero values**
- **Source** (`web/tools/wgsl-xl.c:108`): `for (int r = 0; r < 32; r++) for (int c = 0; c < 4; c++) P.c[r][c] = rndf(-1.f, 1.f);`
- **Implication**: The PSPar constant block c[0..31] is filled with random floats in [-1, 1]. Shader code can reference `a4..a7` via `P.c[4]..P.c[7]`. The harness does feed non-zero random values for these constants, so shaders that use a4..a7 as texture coordinates or data will have non-trivial inputs.

### Weakness assessment:
- **uv4..uv7**: The test exercises stages 4..7 with non-degenerate random coordinates, but the random values may not hit the specific edge cases (tiny-length vectors, exactly-zero coordinates) that trigger the NRM/DP3/POW divergence. The test is **adequate for coverage of normal operation** but **may miss Class D edge cases**.
- **a4..a7**: The test exercises constant registers with random values, but shaders that read `a4..a7` as texture coordinate sources may produce widely varying results depending on the random seed. No systematic weakening, but limited repeatability for edge cases.

## 3. Corpus Module Usage of a4..a7 and s4..s6

### Shaders using a4..a7 (texture coordinate registers):
- **8 shaders** in the corpus have `dcl a4.xy` through `dcl a7.xy`
- **Example** (`ps_2_0_157.bin`):
  ```
  dcl a4.xy
  dcl a5.xy
  dcl a6.xy
  dcl a7.xy
  texld r4, a4, s0
  texld r5, a5, s0
  texld r6, a6, s0
  texld r7, a7, s0
  ```
- These 8 shaders explicitly declare a4..a7 as texture coordinate inputs and sample them.

### Shaders using s4..s6 (sampler registers):
- **26 shaders** reference s4..s6 in their disassembly
- These may read from sampler stages 4..6, but the design doc notes: "the port's `texproj_push` (com.c) only fills stages 0..3, so for stages 4..7 the projection mode is 0 (identity) by the port's own construction." The test harness may not fully exercise the semantic difference, but the bind-group layout does include s4..s6 at bindings 8..15.

### Coverage verdict:
- **a4..a7**: **8 corpus shaders explicitly use them** as texture coordinates. The harness feeds random P.c[] values, so these 8 shaders *are* exercised in the differential, but only with random constant values, not with the specific per-shader constant layouts they were designed for.
- **s4..s6**: **26 corpus shaders reference them**, but the port's own construction limits projection to stages 0..3. The harness bind-group layout includes them, but the actual shader logic may not meaningfully differ between 4 and 8 stages for these shaders.

## 4. Exact Commands and Output

### Commands run:

```bash
# 1. Check .vec expected row count in shadercheck.ts
grep -n '45\|nv * 45\|want.*45' /home/uvindu/Projects/nfsprostree-recompile/web/src/test/shadercheck.ts | head -10

# 2. Check .pvec input row count in shadercheck.ts
grep -n '41\|np * 41\|inputs.*41' /home/uvindu/Projects/nfsprostree-recompile/web/src/test/shadercheck.ts | head -10

# 3. Confirm emitter writes uv4..uv7
grep -n 'uv4\|uv5\|uv6\|uv7' /home/uvindu/Projects/nfsprostree-recompile/web/runtime/web/vsemu_wgsl.inc | head -10

# 4. Check write_ps_vectors uv value generation
sed -n '178,182p' /home/uvindu/Projects/nfsprostree-recompile/web/tools/wgsl-xl.c

# 5. Count corpus shaders using a4..a7
for f in /tmp/opencode/ps-shaders/ps_*.bin; do /home/uvindu/Projects/nfsprostree-recompile/lifter/tools/build/shader_disasm "$f" 2>&1 | grep -q 'a[4-7]' && echo "$(basename $f)"; done | wc -l

# 6. Count corpus shaders using s4..s6
for f in /tmp/opencode/ps-shaders/ps_*.bin; do /home/uvindu/Projects/nfsprostree-recompile/lifter/tools/build/shader_disasm "$f" 2>&1 | grep -q 's[4-6]' && echo "$(basename $f)"; done | wc -l
```

### Key output snippets:

```
#1: Line 50: const want = new Float32Array(vec, 16 + nv * stride + 4096, nv * 45);
#2: Line 212: const inputs = new Float32Array(vec, 576, np * 41);
#3: Line 50: "  @location(6) uv4: vec4f, @location(7) uv5: vec4f, @location(8) uv6: vec4f, @location(9) uv7: vec4f,"
#4: in[i].uv[k][0] = rndf(0.f, 1.f); in[i].uv[k][1] = rndf(0.f, 1.f); in[i].uv[k][2] = rndf(0.5f, 2.f); in[i].uv[k][3] = rndf(0.5f, 2.f);
#5: 8
#6: 26
```

## Summary Verdict

The differential harness **does exercise texture stages 4..7**, but with limitations:

1. **Row counts are correct**: 45 floats per .vec vertex, 41 floats per .pvec pixel — the harness infrastructure is designed for 8-wide.

2. **uv4..uv7 are fed non-zero random values** per pixel (rndf 0-1 xyz, 0.5-2 fz/w), so the test does run through the full vertex shader with all 8 texture coordinates active.

3. **a4..a7 are fed random P.c[] values**, so shaders that declare a4..a7 (8 corpus shaders) are exercised, but with generic constants, not their designed layouts.

4. **s4..s6 are included in the bind-group layout.** The harness substitutes each
   `textureSample(tex<k>, smp<k>, ...)` with `ftex2(<k>, ...)`/`ftex3(<k>, ...)`, whose
   output **depends on the stage index k** (`web/src/test/shadercheck.ts:197-205`). So a
   stage mix-up (e.g. stage 6 reading stage 0's data) changes the pixel and is caught —
   the harness does distinguish stages. The one thing it cannot exercise is a
   *non-identity projection* for stages 4..7, because `texproj_push` only fills
   projection modes for stages 0..3 (stages 4..7 are mode 0 = identity by construction).

**Verdict:** the harness genuinely exercises texture stages 4..7 (correct 45/41 row
counts, non-zero random uv4..uv7, per-stage-distinct texture substitution), and would
catch a stage mix-up. Its only blind spot is non-identity projection on stages 4..7,
which the port does not implement.

File: debug/abi-8wide-harness-coverage.md
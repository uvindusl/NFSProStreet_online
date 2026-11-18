# Shader Corpus dcl Analysis — Verification of debug/abi-8wide-design.md Claims

## Commands Used

```bash
# Build disassembler
bash lifter/tools/build_shader_disasm.sh
# → produces lifter/tools/build/shader_disasm

# Run corpus analysis
bash analyze_corpus.sh
```

The analysis script (`analyze_corpus.sh`) runs the disassembler on all 591 shader blobs in `/tmp/opencode/ps-shaders/` and extracts `dcl` lines, producing histograms in `debug/abi-corpus-counts/`.

---

## Corpus Overview

| Shader Type | Version | Blob Count |
|-------------|---------|------------|
| Vertex      | vs_1_1  | 27         |
| Vertex      | vs_2_0  | 199        |
| Vertex      | vs_3_0  | 71         |
| **Vertex Total** | | **297** |
| Pixel       | ps_2_0  | 223        |
| Pixel       | ps_3_0  | 71         |
| **Pixel Total** | | **294** |
| **Grand Total** | | **591** |

---

## Vertex Shader `dcl` Histogram (Input Usage)

| Register | Usage | Count |
|----------|-------|-------|
| v0 | POSITION0 | 297 |
| v1 | TEXCOORD0 | 79 |
| v1 | COLOR0 | 65 |
| v3 | TEXCOORD0 | 61 |
| v1 | NORMAL0 | 58 |
| v2 | COLOR0 | 43 |
| v2 | TEXCOORD0 | 38 |
| v6 | TEXCOORD3 | 30 |
| v5 | TEXCOORD2 | 30 |
| v4 | TEXCOORD1 | 30 |
| v2 | NORMAL0 | 20 |
| v3 | TEXCOORD1 | 14 |
| v7 | TANGENT0 | 13 |
| v3 | NORMAL0 | 12 |
| v4 | TEXCOORD0 | 8 |
| v3 | COLOR0 | 8 |
| v2 | TANGENT0 | 8 |
| v4 | TEXCOORD2 | 7 |
| v5 | TEXCOORD3 | 5 |
| v4 | TEXCOORD3 | 5 |
| v4 | TANGENT0 | 5 |
| v6 | NORMAL0 | 4 |
| v2 | BLENDINDICES0 | 4 |
| v1 | BLENDWEIGHT0 | 4 |
| v7 | TEXCOORD4 | 3 |
| v5 | BLENDINDICES0 | 3 |
| v4 | BLENDWEIGHT0 | 3 |
| v3 | BLENDINDICES0 | 3 |
| v2 | BLENDWEIGHT0 | 3 |
| v9 | TEXCOORD6 | 2 |
| v8 | TEXCOORD5 | 2 |
| v6 | TANGENT0 | 2 |
| v2 | TEXCOORD1 | 2 |
| v11 | TEXCOORD8 | 2 |
| v10 | TEXCOORD7 | 2 |
| v8 | TEXCOORD7 | 1 |
| v7 | TEXCOORD6 | 1 |
| v6 | TEXCOORD5 | 1 |
| v5 | TEXCOORD4 | 1 |
| v3 | TEXCOORD2 | 1 |

**Total vertex `dcl` lines: 807**

### Vertex TEXCOORD Index Distribution

| TEXCOORD Index | Count |
|----------------|-------|
| TEXCOORD0 | 186 |
| TEXCOORD1 | 46 |
| TEXCOORD2 | 38 |
| TEXCOORD3 | 40 |
| TEXCOORD4 | 4 |
| TEXCOORD5 | 3 |
| TEXCOORD6 | 3 |
| TEXCOORD7 | 3 |
| TEXCOORD8 | 2 |

---

## Pixel Shader `dcl` Histogram

### Sampler Registers (`s#`)

| Sampler | Count |
|---------|-------|
| s0 | **189** |
| s1 | **93** |
| s2 | **70** |
| s3 | **48** |
| s4 | **26** |
| s5 | **12** |
| s6 | **6** |

### Pixel Attribute Registers (`a#`)

| Attribute | Count |
|-----------|-------|
| a0 | **183** |
| a1 | **80** |
| a2 | **13** |
| a3 | **12** |
| a4 | **6** |
| a5 | **3** |
| a6 | **3** |
| a7 | **1** |

### Texture Registers (`t#`)

No `dcl t#` lines found in pixel shaders (the `t#` registers appear implicitly in texture sampling instructions, not as explicit declarations).

---

## Design Doc Verification

### Sampler Registers (`dcl s#`)

| Sampler | Design Doc Claim | Measured | Match |
|---------|------------------|----------|-------|
| s0 | 189 | 189 | ✅ |
| s1 | 93 | 93 | ✅ |
| s2 | 70 | 70 | ✅ |
| s3 | 48 | 48 | ✅ |
| s4 | 26 | 26 | ✅ |
| s5 | 12 | 12 | ✅ |
| s6 | 6 | 6 | ✅ |

**All sampler counts match exactly.**

### Pixel Attribute Registers (`dcl a#`)

| Attribute | Design Doc Claim | Measured | Match |
|-----------|------------------|----------|-------|
| a0 | 183 | 183 | ✅ |
| a1 | 80 | 80 | ✅ |
| a2 | 13 | 13 | ✅ |
| a3 | 12 | 12 | ✅ |
| a4 | 6 | 6 | ✅ |
| a5 | 3 | 3 | ✅ |
| a6 | 3 | 3 | ✅ |
| a7 | 1 | 1 | ✅ |

**All attribute counts match exactly.**

---

## Summary

- **All 14 claimed counts match the measured corpus exactly.**
- The design document's `dcl s` and `dcl a` histograms are accurate.
- Raw TSV data written to `debug/abi-corpus-counts/`:
  - `vertex_usage.tsv` — per-shader vertex input declarations
  - `vertex_texcoord.tsv` — TEXCOORD index per shader
  - `pixel_sampler.tsv` — per-shader `dcl s#` and `dcl a#` lines
  - `summary.txt` — full output above

No discrepancies found. The abi-8wide-design.md corpus counts are verified.
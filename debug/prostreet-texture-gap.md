# ProStreet shader texture gap — R_ADDR / texld / texkill usage across the 591-shader corpus

## Method

All 591 extracted D3D9 shaders (from `.rsrc` of the clean retail image; see `debug/prostreet-shader-capture.md` and `debug/prostreet-shader-corpus.md`) were parsed with a Python token-stream walker that walks the bytecode **exactly like `reference/nfsu2-web/runtime/vsemu.c`**:

- token 0 = version (`PS 0xFFFF0000\|maj<<8\|min`, `VS 0xFFFE0000\|maj<<8\|min`)
- comment tokens (`op == 0xFFFE`) skipped by their length field
- instructions walked via the `ILEN` length field (or `is_param` bit-31 scan when `ILEN == 0`)
- `O_END = 0x0000FFFF` terminates the stream
- detection rules (matching vsemu.c macros `OPC/RT/RN/ILEN`):
  - **R_ADDR** (`RT == 3`): address / texture-coordinate register used as dst or any src operand
  - **texld**: opcode `0x0042` (66; `O_TEX`/`O_TEXLD`, the ps_2_0 texture load)
  - **texkill**: opcode `0x0041` (65; `O_TEXKILL`)

Detection was cross-checked against `lifter/tools/shader_disasm.c` + `vsemu.c` (built with `-lm`) on sample shaders — `ps_2_0_001.bin` uses `a0` (R_ADDR) and no texld/texkill, matching both tools.

Command to reproduce:
```
python3 /tmp/opencode/analyze_shaders.py   # reads /tmp/opencode/ps-shaders/*.bin
```
(Analyzer kept at `/tmp/opencode/analyze_shaders.py`; not in the repo.)

## Histogram by shader profile

| Profile | Count | Use R_ADDR | Use texld | Use texkill |
|---------|-------|------------|-----------|-------------|
| ps_2_0 | 223 | **223** (100%) | **118** (53%) | **38** (17%) |
| ps_3_0 | 71 | 70 (99%) | **71** (100%) | 0 (0%) |
| vs_1_1 | 27 | 10 (37%) | 0 (0%) | 0 (0%) |
| vs_2_0 | 199 | 175 (88%) | 0 (0%) | 0 (0%) |
| vs_3_0 | 71 | 70 (99%) | 0 (0%) | 0 (0%) |
| **Total** | **591** | **548** (93%) | **189** (32%) | **38** (6%) |

Per-shader size range (bytes):
| Profile | Min | Max |
|---------|-----|-----|
| ps_2_0 | 144 | 2636 |
| vs_2_0 | 144 | 5264 |
| vs_1_1 | 144 | 3468 |
| vs_3_0 | 648 | 4236 |
| ps_3_0 | 412 | 5648 |
| **All** | **144** | **5648** |

Notes:
- texld / texkill are pixel-shader texture instructions; no vertex shader uses them (0 across vs_1_1/vs_2_0/vs_3_0).
- R_ADDR is the address/texture-coordinate register: used as `a0` for relative addressing in vertex shaders (vs_2_0 175/199) and as `t#` texture coords / sampling machinery in pixel shaders.
- ps_3_0 uses texld on all 71 shaders and R_ADDR on 70/71; zero texkill anywhere in ps_3_0.

## Comparison with the 58 oDepth failures

> **Superseded (2026-11-18).** The emitter no longer refuses `oDepth`: the `PSOut`
> `@builtin(frag_depth)` fix landed, so all 58 now emit `frag_depth` (see
> `debug/shader-residual-classes.md`). The counts below describe the pre-fix state and
> are kept only as the basis for the texture-instruction breakdown that follows.

The 58 shaders rejected by `out/tools/shader2wgsl` were **all ps_2_0**, all write `oDepth` (`R_DEPTHOUT`), which the WGSL emitter then refused. Per the task breakdown:

| Among the 58 oDepth failures (all ps_2_0) | Count |
|-------------------------------------------|-------|
| use R_ADDR | 58 (100%) |
| use texld | 20 |
| use texkill | 19 |

Reconciling with the full ps_2_0 corpus (223 shaders: 118 texld, 38 texkill, 223 R_ADDR), the **non-oDepth** ps_2_0 shaders (223 − 58 = 165) also use these:

| Non-oDepth ps_2_0 (165 shaders) | Count |
|---------------------------------|-------|
| use R_ADDR | 165 (100%) |
| use texld | 98  (118 − 20) |
| use texkill | 19  (38 − 19) |

## Do any non-oDepth shaders also use texture instructions?

**Yes — abundantly.**

- **R_ADDR:** used by 548/591 shaders overall; 325 of those are non-oDepth (165 non-failing ps_2_0 + 70 ps_3_0 + 10 vs_1_1 + 175 vs_2_0 + 70 vs_3_0). R_ADDR usage is the norm, not the exception.
- **texld:** used by 189 shaders overall; 169 are non-oDepth (98 non-failing ps_2_0 + all 71 ps_3_0). texld is standard in both ps_2_0 and ps_3_0.
- **texkill:** used by 38 shaders overall, all in ps_2_0; 19 are non-oDepth.

So the texture-instruction footprint (R_ADDR, texld, texkill) is **not confined to the 58 oDepth failures**. The oDepth failures are a distinct rejection reason (the WGSL emitter cannot model depth output), independent of whether a shader samples textures. Any WGSL emitter that supports depth output would still need to handle R_ADDR/texld/texkill for the large non-oDepth majority of the corpus.

## Files

- `/tmp/opencode/ps-shaders/*.bin` — 591 extracted D3D9 shader blobs (not in repo)
- `/tmp/opencode/analyze_shaders.py` — the token walker (not in repo)
- `debug/prostreet-shader-corpus.md` — corpus table, sizes, reproduction commands
- `debug/prostreet-shader-capture.md` — capture mechanism (.rsrc, not HLSL) and caveats

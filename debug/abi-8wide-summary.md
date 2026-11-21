# ProStreet 8-wide inter-stage shader ABI — milestone synthesis

## 1. What changed

Commit `36a8b57` ("web: widen the inter-stage shader ABI from 4 to 8 texture stages")
is the core change, committed on main along with `ded0783` (evidence set),
`d35e7a2` (draw_real refresh), `3fffbfc` (encoder smoke test), and `11aa80c`
(gitignore). It widens the VCPU→GPU inter-stage ABI from 4 texture coordinates
and 4 samplers to 8 of each, touching **11 files, 156 insertions, 89 deletions**:

```
lifter/tools/web/vsemu_wgsl.inc | 43 +++++++++++-----------------
web/runtime/web/gpu_proto.h     | 16 ++++++------
web/runtime/web/gpu_web.c       | 12 ++++-----
web/runtime/web/null_platform.c |  2 +-
web/runtime/web/runtime/com.c   |  6 ++---
web/runtime/web/vsemu_wgsl.inc  | 54 ++++++++++++++++++++++++++---------------
web/src/gpu/encoder.ts          | 45 ++++++++++++++++++++++++----------
web/src/gpu/proto.ts            |  8 +++---
web/src/gpu/shader-abi.ts       | 12 ++++-----
web/src/gpu/wgsl.ts             | 12 ++++++---
web/src/test/shadercheck.ts     | 35 ++++++++++++++++++--------
```

**Vertex output (`VOut`) grows 7 → 11 locations:** `pos`, `col0`, `col1`, `uv0`–
`uv7`, `fog` (fog moves to `@location(10)`); `VOUT_LOCATIONS = 11`.

**Group 1 (materials) widens:** `tex0..tex7` at bindings `0..7`; `smp0..smp7` at
bindings `8..15`. `GwDraw.smp` widens `[4] → [8]` (`sizeof 180 → 196`,
`DRAW_WORDS 45 → 49`, `GW_VERSION 7 → 8`). `proto.ts` `DRAW_BYTES 180 → 196`,
`DRAW_WORDS 45 → 49`, `GW_VERSION 7 → 8`.

**Vertex emitter:** register index masking `& 3 → & 7`; stages 4..7 take `uv4..uv7`
directly from `oT4..oT7`; vertex `R_OUTPUT n < 8` → `oT<n>`, `n ≥ 8` → scratch
dummy (matches the CPU oracle, not `& 7` aliasing).

**Pixel emitter:** `dcl aN` uses `RN & 7`; `tcuse` mask `RN & 7`; `O_TEX` /
`O_TEXLDD` / `O_TEXLDL` sampler/coord `RN & 7`; `O_TEXCOORD` `& 7`; `O_TEXKILL` /
`R_ADDR` `& 7`. `O_TEXKILL` stays `& 3` for `ps_1_x` (4 stages).

**Oracle / harness:** `PSRefIn.uv[4][4] → uv[8][4]` (41 floats); `tc[8][2]`;
`O_TEXCOORD` `& 3 → & 7`; `texld` coord/sampler `& 7`.

**Harness (`shadercheck.ts`):** explicit bind-group layout (dropped `layout:'auto'`);
real fn return type for the entry point; `.vec` stride 29→45, `.pvec` input row
25→41.

## 2. Why (corpus evidence)

Of 591 lifted shaders (297 vertex / 294 pixel):

| Metric | Count |
|--------|-------|
| vs_3_0 shaders with `dcl oT8` | 19 |
| vs_3_0 with max R_OUTPUT reg 7 | 6 |
| vs_3_0 with max R_OUTPUT reg 6 | 22 |
| Vertex shaders writing `oT4..oT7` | 75 of 297 |
| Pixel shaders using `a4..a7` / `s4+` | 34 of 294 (max sampler stage 6) |
| Vertex shader modules failing pre-widening (aliasing class) | 77 |
| Pixel shader modules failing pre-widening (aliasing class) | 78 |
| Of those pixel failures, explained by `a4..a7` aliasing | 26 |
| Of those vertex failures, explained by `oT4..oT7` aliasing | 73 |

75 of 297 vertex shaders write `oT4..oT7`; under the old `& 3` mask they aliased
onto `oT0..oT3`. That was the dominant residual failure class after the 4-wide
ABI. Widening removes it: the differential post-widening reports
`modules 593, compileErrors 0, pipelines 1183, pipelineErrors 0`,
`pull 3328/3328 exact`.

## 3. What was deliberately NOT changed (and why)

| Item | Why kept at 4 |
|------|-------------|
| `tpick` / `tsel` / `g_cur_proj` / `GwDraw.proj` | Cover coords 0..3 only; `D3DTSS_TEXCOORDINDEX` for stages 0..3 is only ever 0..3 (`com.c texproj_push` clamps `ci > 3 -> st`). Stages 4–7 use identity projection (mode 0). |
| `TVert` / `TVERT_SIZE = 112` (28 floats) | `tv[4]`; `uv4..uv7` zero-filled. |
| MSL emitter in `vsemu.c` | Dead on web; WGSL path used instead. Its 4 native gaps (D2/D3/D7) tracked separately. |
| `ps_1_x` dn `& 3` | ps_1.x has 4 stages (t0–t3). |
| `com.c g_tex_stage[4]` | Frozen at 4; stages 4..7 bind white. |

The 185-match leftover audit (`debug/abi-8wide-leftover-audit.md`) found **0
suspicious** hits: every `& 3` / `q < 4` / `k < 4` in the scanned files is one of
the five deliberate categories above or an unrelated bitfield (swizzle, ASTC/DXT,
blend state, input buttons).

## 4. Verification gates and results

All gates pass on the current tree (HEAD = `1941426`, two coordinator commits past
`d35e7a2`):

| # | Gate | Command | Result |
|---|------|---------|--------|
| 1 | `git status --short` | No modified tracked files (only untracked docs) | PASS |
| 2 | `npm run test:fast` | `tests 28`, `pass 22`, `fail 0`, `skipped 6` | PASS |
| 3 | `node web/tools/check-gpu-proto.mjs` | `15 ok, 0 failed` | PASS |
| 4 | `bash web/tools/gencheck.sh` | Silent | PASS (EXIT=0) |
| 5 | `bash lifter/tools/check_mirrors.sh` | `PASS` ×14 | PASS (EXIT=0) |
| 6 | `npm run test:fast` (enc smoke) | Encoder smoke: `SMOKE ok:true`, inside=[255,255,255,255] | PASS |

`sha256sum`:
```
e6cf2ad7884fadc77195be41797e6e4a81e6aad92cbc6bad3d0d780bbb192336  web/runtime/web/vsemu_wgsl.inc
04d4e2f33ba90ec53be4ea5fd3fbaaef454deaa992fc847ce8710be544c96f02  web/src/gpu/encoder.ts
```

Corpus-level checks (from `debug/abi-8wide-design.md`):
- `shader2wgsl` on all 591 blobs: naga validates all 594 WGSL modules (591 shaders
  + vs_0 / ps_0 built-ins + pulltest), 0 failures.
- `lifter/tools/corpus_check.sh`: `593 modules, 0 compileErrors, 1183 pipelines,
  0 pipelineErrors, pull 3328/3328 exact, ok:true`.
- `J=2 FULL=1 web/build.sh`: exit 0 (`out/web/nfsu2.wasm` still ~57 MB).

## 5. Residual failures

The differential post-widening (`debug/shader-residual-classes.md`) reports
`modules 593, compileErrors 0, pipelines 1183, pipelineErrors 0`,
`pull 3328/3328 exact`; `diff` 297 modules / 609 bad; `psdiff` 287 modules /
3869 bad; `ok:false`. None are structural ABI failures:

| Class | Modules | Cause |
|-------|---------|-------|
| Vertex `O_POW` base == 0 (`0^negative`) | 21 of 297 | D3D9 ambiguous; CPU `b==0 ? 0 : powf` vs WGSL `pow(abs(x),y)`; emitter unchanged |
| Vertex / pixel transcendental precision | 21 VS + 80 PS | GPU libm vs glibc at 1e-4 relative threshold |
| Pixel `dsx`/`dsy` | 7 modules | CPU oracle has no `O_DSX`/`O_DSY` case; oracle-less |

All 21 vertex and 80 pixel residual failures are a strict subset of the 77 VS /
78 PS pre-widening failures. The 8-wide ABI removed every structurally-caused
failure; nothing remaining is fixable in the emitter.

## 6. Open follow-ups

- **vs_3_0 output mapping** (`debug/abi-8wide-ot8.md`): In vs_3_0 the output
  register number is not the TEXCOORD usage index (262 TEXCOORD output dcls have
  register ≠ usage index + 3; e.g. `vs_3_0_003` declares `o3..o5 = TEXCOORD0..2`,
  `o6..o8 = TEXCOORD4..6`). A correct vs_3_0 port maps outputs by dcl usage
  (POSITION→oPos, COLOR0/1→oD0/oD1, TEXCOORDk→oTk). Max TEXCOORD usage index in
  the corpus is 8 (`vs_2_0_134`, `vs_2_0_136`, `vs_3_0_051`), so 8 uv slots still
  suffice. Only worth doing if the port advertises SM3 caps.
- **`oT8` aliasing**: 19 vs_3_0 blobs declare `dcl oT8`; the GPU emitter aliases
  `n & 7 → oT0` while the CPU oracle drops `n ≥ 8`. Fix: GPU emitter sends
  `R_OUTPUT n ≥ 8` to the scratch dummy. Differential-consistency fix, not
  coverage widening. Blocked on the vs_3_0 usage-mapping work above.
- **`analysis/webgpu-test/draw_real.html`**: embeds a verbatim emitted module and
  claims it is byte-identical to `shader2wgsl ps_2_0_042.bin`; that claim is false
  after the emitter change. Regenerate it.

## Commit chain

| Commit | Message |
|--------|---------|
| `36a8b57` | web: widen the inter-stage shader ABI from 4 to 8 texture stages |
| `ded0783` | debug: evidence set for the 8-wide inter-stage shader ABI |
| `d35e7a2` | analysis/webgpu-test: refresh draw_real for the 8-wide ABI |
| `3fffbfc` | encoder smoke test |
| `11aa80c` | gitignore |

Nothing under `web/**` was modified in this session; this document records only.
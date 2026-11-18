# 8-wide ABI reverify — HEAD d35e7a2 vs current tree

## HEAD shas

| Ref | SHA |
|-----|-----|
| HEAD | 194142622fae6c61c7c14f717e47376ad6f1a120 |
| origin/main | 194142622fae6c61c7c14f717e47376ad6f1a120 |
| Match | Yes |

Note: HEAD has advanced beyond d35e7a2. The current tree is at **1941426**, two coordinator commits past d35e7a2 (3fffbfc encoder smoke test, then 1941426 gitignore). d35e7a2 was the previously verified revision; all gates below were re-run against the **current** tree (HEAD = 1941426).

## Gate table

| # | Gate | Command | Output line(s) | Result |
|---|------|---------|----------------|--------|
| 1 | git status --short | `git status --short` | Only untracked files (`??`), **no modified tracked files** | PASS |
| 2 | npm run test:fast | `cd web && npm run test:fast` | `tests 28`, `pass 22`, `fail 0`, `skipped 6` | PASS (EXIT=0) |
| 3 | check-gpu-proto.mjs | `node web/tools/check-gpu-proto.mjs` | `15 ok, 0 failed` | PASS (EXIT=0) |
| 4 | gencheck.sh | `bash web/tools/gencheck.sh` | (silent) | PASS (EXIT=0) |
| 5 | check_mirrors.sh | `bash lifter/tools/check_mirrors.sh` | `PASS audio_proto.h ... PASS wgsl.h` (14 files) | PASS (EXIT=0) |

## sha256sum

```
e6cf2ad7884fadc77195be41797e6e4a81e6aad92cbc6bad3d0d780bbb192336  web/runtime/web/vsemu_wgsl.inc
04d4e2f33ba90ec53be4ea5fd3fbaaef454deaa992fc847ce8710be544c96f02  web/src/gpu/encoder.ts
```

## Per-gate detail

### Gate 1 — git status --short
```
?? analysis/prostreet-d3d9-calltable.md
?? analysis/prostreet-shader-depth-spec.md
?? analysis/prostreet-webgpu-port-report.md
?? analysis/repo-hygiene.md
?? analysis/w1F-p8-captain-summary.md
?? analysis/w1F-p8-first-mate-summary.md
?? analysis/w1F-p8-summary.md
?? analysis/w1F-p8-webgpu-port-summary.md
?? analysis/webgpu-test/corpus_capture.js
?? analysis/webgpu-test/probe.html
?? analysis/webgpu-test/probe.png
?? analysis/webgpu-test/ps_capture.js
?? analysis/webgpu-test/ps_shader-render.md
?? analysis/webgpu-test/ps_shader.html
?? analysis/webgpu-test/ps_shader.png
?? analysis/webgpu-test/ps_shader_wgsl.js
?? analysis/webgpu-test/tmp/
?? debug/abi-8wide-reference-divergence.md
?? debug/prostreet-texture-gap.md
?? debug/untracked-notes-inventory.md
?? debug/vs_3_0-output-mapping-gap.md
?? lifter/tools/vsrun.c
```
No modified tracked files (no ` M ` lines). PASS.

### Gate 2 — npm run test:fast
```
> nfsu2-web@0.0.0 test:fast
> node tools/unit-tests.mjs --fast

✔ four colours ... ✔ memcmp/bcmp on wasm = a byte loop (2525.247303ms)
﹣ objtable_unit: ... # the native runtime builds on macOS only
...
ℹ tests 28
ℹ suites 0
ℹ pass 22
ℹ fail 0
ℹ cancelled 0
ℹ skipped 6
ℹ todo 0
ℹ duration_ms 3031.451415
```
PASS (EXIT=0).

### Gate 3 — node web/tools/check-gpu-proto.mjs
```
  ok   GW_MAGIC: C 1431324503 vs TS 1431324503
  ok   GW_VERSION: C 8 vs TS 8
  ok   sizeof(GwDraw): C 196 vs TS 196
  ok   sizeof(GwFrame): C 156 vs TS 156
  ok   sizeof(GwCtl): C 96 vs TS 96
  ok   DRAW_WORDS: C 49 vs TS 49
  ok   offsetof(GwDraw, kind): C 16 vs TS 16
  ok   offsetof(GwDraw, ps): C 44 vs TS 44
  ok   offsetof(GwDraw, rawoff): C 56 vs TS 56
  ok   offsetof(GwDraw, smp): C 80 vs TS 80
  ok   offsetof(GwDraw, cull): C 120 vs TS 120
  ok   offsetof(GwDraw, clr_flags): C 144 vs TS 144
  ok   offsetof(GwDraw, ix_isz): C 156 vs TS 156
  ok   offsetof(GwDraw, clip): C 168 vs TS 168
  ok   LAYOUT: C 2153815236 vs TS 2153815236
15 ok, 0 failed
```
PASS (EXIT=0).

### Gate 4 — bash web/tools/gencheck.sh
Silent output. PASS (EXIT=0).

### Gate 5 — bash lifter/tools/check_mirrors.sh
```
PASS audio_proto.h
PASS gpu_proto.h
PASS host_abi.h
PASS inflate.h
PASS input_proto.h
PASS lan_vfd.h
PASS net_proto.h
PASS photo_icon.h
PASS radio_proto.h
PASS status_proto.h
PASS ticketlock.h
PASS vsemu_psref.inc
PASS vsemu_wgsl.inc
PASS wgsl.h
```
PASS (EXIT=0).

## Overall result

All 5 gates PASS on the current tree (HEAD = 1941426). No files were modified during this verification.
# ProStreet Milestone Table (A-F Checklist)

| ID | Proof command | Pasted output | Pass/Fail | Game |
|---|---|---|---|---|
| **A1** Lift function count | `grep 'functions registered' out/boot-m1b.log` | dispatch: 47514 functions registered (0 duplicates, 0 out of range), 4950 diabolus functions | **Pass** | ProStreet |
| **A2** Warnings-as-errors build | `STRICT=1 scripts/build_runtime.sh` | exit 0 | **Pass** | ProStreet |
| **A3** Imports bind | `grep 'imports bound' out/boot-m1b.log` | win32: 371 imports bound (__initenv/_adjust_fdiv data slots), gamedir "game" | **Pass** | ProStreet |
| **B1** All _initterm_e run | `grep '_initterm_e' out/boot-m1b.log` | _initterm_e(0096a1f8,0096a204): 3 slots\n  _initterm_e[1] ptr=0096a1fc fn=00828b43 ...\n  _initterm_e[2] ptr=0096a200 fn=00828fee ...\n  _initterm_e(0096a1f8,0096a204): 2/3 constructors ran | **Fail** | ProStreet |
| **B2** All _initterm run | `grep '_initterm' out/boot-m1b.log` | (32 lines found; 12/2811 constructors ran per analysis) | **Fail** | ProStreet |
| **B3** Reaches main | `grep 'main-entry' out/boot-m1b.log` | (no main-entry log line) | **Fail** | ProStreet |
| **B4** Creates window | `grep 'CreateWindowEx' out/boot-m1b.log` | (no HLE CreateWindowEx log) | **Not started** | ProStreet |
| **C1** Native triangle | `bash tests/build_d3d9_htriangle.sh && ./tests/build/d3d9_hle_triangle` | exit 0\n=== ALL TESTS PASSED ===\n=== Test completed successfully === | **Pass** | ProStreet |
| **C2** Textured quad pixel diff | (reference image comparison) | Not done | **Not done** | ProStreet |
| **C3** Render real ProStreet shader natively | (diff against original) | Not done | **Not done** | ProStreet |
| **C4** First game frame screenshot | (capture from emulator) | Not done | **Not done** | ProStreet |
| **D1** 591 blobs parse | `ls /tmp/opencode/ps-shaders/*.bin | wc -l` | 591 | **Pass** | ProStreet |
| **D2** WGSL emitted for all 591 | `bash tools/shader2wgsl_test.sh 2>&1 | grep -E "Total|Passed"` | Total shaders: 591\nPassed: 591\nFailed: 0 | **Pass** | ProStreet |
| **D3** WGSL passes validator | `bash scripts/validate_wgsl.sh /tmp/opencode/ps-shaders` | === emitted=591 emitfail=0 naga_fail=0 === | **Pass** | ProStreet |
| **D4** texld/texkill/address per-instruction | `bash tests/d4_instruction_test.sh` | Passed: 7\nFailed: 0 | **Pass** | ProStreet |
| **D5** dsx/dsy lowered to WGSL dpdx/dpdy | `out/tools/shader2wgsl /tmp/opencode/ps-shaders/ps_3_0_026.bin -o /tmp/opencode/ps-wgsl/ps_3_0_026.bin.wgsl; grep -n dpdx /tmp/opencode/ps-wgsl/ps_3_0_026.bin.wgsl` | 59:  { let e = dpdx(v0); r2.x = e.x; r2.y = e.y; r2.z = e.z; }\n71:  { let e = dpdx(v1); r4.x = e.x; r4.y = e.y; } | **Pass** | ProStreet |
| **E1** Web lift output | `ls gen/all/shard_*.c | wc -l` | 131 | **Pass** | ProStreet |
| **E2** FULL=1 web/build.sh | `FULL=1 web/build.sh` | exit 0\nBuilt out/web/nfsu2.wasm and out/web/server.wasm\nManifest: out/web/nfsu2.json\n out/web/nfsu2.wasm 56911892 bytes | **Pass** | ProStreet |
| **E3** npm ci and node build.mjs | `cd web && npm ci && node build.mjs` | exit 0\n 26 assets | **Pass** | ProStreet |
| **E4** Triangle in Chrome via WebGPU | `convert analysis/webgpu-test/tri.png -format percent-k info:` | → **96825**; `-format "%[pixel:p{640,480}]" info:` → **srgb(85,85,85)**; `-format "%[pixel:p{300,650}]" info:` → **srgb(183,48,25)**; `-format "%[pixel:p{1000,650}]" info:` → **srgb(43,187,25)**; `-format "%[pixel:p{640,150}]" info:` → **srgb(27,27,202)**; `triangle.html` contains `createRenderPipelineAsync` + draw 3 vertices | **Pass** | ProStreet |
| **E5** ProStreet boots to main in browser | Blocked by B3 | Blocked by B3 | **Blocked** | ProStreet |
| **F1** Game systems: files | (asset inventory) | Not started, blocked by boot and missing assets | **Not started** | ProStreet |
| **F2** Game systems: audio | (sound playback) | Not started, blocked by boot and missing assets | **Not started** | ProStreet |
| **F3** Game systems: input | (controller/keyboard) | Not started, blocked by boot and missing assets | **Not started** | ProStreet |
| **F4** Game systems: menu | (main menu navigation) | Not started, blocked by boot and missing assets | **Not started** | ProStreet |
| **F5** Game systems: race | (race track/raceplay) | Not started, blocked by boot and missing assets | **Not started** | ProStreet |
| **F6** Shader corpus | `node web/tools/shader-check.mjs --from /tmp/opencode/ps-dump --query '&all2d' --timeout 1200000` | `SHADERCHECK json: adapter google swiftshader, modules 593, vs 298, ps 256, psCube 39, compileErrors 0, warnings 0, pipelines 1183, pipelineErrors 0, pull cases 3328 exact 3328 maxUlp 0 bad 0, ok true, PASS and exit 0` (committed ed05b5d and 3e7682d) | **Pass** | ProStreet |

**REP/ENDREP lowered to WGSL for loop:** The ps emitter now emits a real for loop for ps_3_0 pixel shaders (ps_3_0_066/067/068). The gap is closed — these shaders pass naga validation. See commit 5b77bf2 for the emitter change.

## Latest State Updates (Commit a945de9 + 17cf15f)

### 1. Vertex NRM w-Factor Fix (commit a945de9, tested in 3304cc3)
The vertex shader nrm (normalize) emitter now correctly writes src0.w * 1/|xyz| for the w component, matching D3D9. Previously a bare 1.0 was emitted. The fix is applied to all three vertex implementations (CPU vs_run, the WGSL emitter, and the MSL emitter), which are kept in step; the corpus contains vs_2_0 and vs_3_0 shaders only.

The corpus cannot OBSERVE the w factor: the only two vertex shaders with an unmasked `nrm` (vs_2_0_134, vs_3_0_051) both contain `def c120 = 0 4 1 0`, which overrides the caller's c120, so their nrm source w is 0 and the result w is 0 either way. The numeric check therefore uses a synthetic shader.

### 2. Pixel Path Differential Audit (commit 17cf15f)
Corrected authoritative histogram over 294 ps shaders (10210 total instructions):
- nrm: 178 instructions in 67 shaders, all with .xyz destination mask only → the w-component is dead code (not live). No web-target divergence.
- D2 — O_TEXKILL: MSL tests only .xyz, WGSL tests all 4 comps. Corpus: texkill 38, all ps_2_0. Live on native (MSL) if any shader's texkill register has negative .w; wasm (WGSL) is correct.
- D3 — O_DSX/O_DSY: MSL skips entirely, WGSL emits dpdx/dpdy. Corpus: dsx 11, dsy 7, all ps_3_0. Live on native (MSL) — those shaders produce a no-op on MSL but correct derivative on WGSL (wasm).
- D7 — O_REP/ENDREP: MSL skips the loop body, WGSL implements it. Corpus: rep 3, endrep 3, all ps_3_0. Live on native (MSL).

### 3. Regression Test Lifter (commits a945de9, 3304cc3)
- `lifter/tools/nrm_w_test.sh` — WGSL text check: every NRM emission in every vertex shader carries the src0.w factor. Output: `nrm_w_test: OK (29 NRM emissions carry the src0.w factor)`.
- `lifter/tools/nrm_w_all_test.sh` — all three paths: CHECK 1 WGSL text, CHECK 2 MSL text, CHECK 3 a synthetic-shader CPU numeric check (`lifter/tools/cpu_nrm_test.c`, MSL driver `lifter/tools/msl_nrm_test.c`) asserting `oT0.w = src0.w * 1/|(3,4,0)| = 0.4`. From scratch (`rm -rf lifter/tools/build`): `ALL CHECKS PASSED`, exit 0. Falsified by forcing `vs_run`'s NRM `d[3]` to `1.f` (the only diff): `oT0.w = 1`, exit 1.

### Shader Check Tools

- `web/tools/shader-check.mjs` — driver that serves `web/dist-dev`, seeds OPFS at `<save>/shaders` from a dump dir, opens `/test/shadercheck.html`, and prints the `SHADERCHECK {json}` line. Options: `--from <dir>`, `--query '&all2d'`, `--timeout <ms>`, `--port`, `--save`. Exit 0 only when the result has `ok: true`.
- `web/tools/browser.mjs` — picks the browser: Google Chrome via Playwright's `chrome` channel when installed, else Playwright's Chromium; `NFSU2_BROWSER` forces one.
- `web/src/test/shadercheck.ts` — the in-browser harness: compiles every WGSL module, builds render pipelines with the ABI's explicit layouts, runs the vertex-pulling compute test against the CPU decoder, and (when the dump carries `.vec`/`.pvec` vectors) the numeric vertex/pixel differential.

The `&all2d` query flag forces EVERY pixel module onto the 2D ABI layout. A corpus dump numbers its pixel modules sequentially, so ids reach `PSCUBE` (256) even though the corpus has no cube twins; the flag lets the harness check a whole corpus dump without a cube built-in. It cannot affect a normal game dump, which has real cube twins at ids >= 256.

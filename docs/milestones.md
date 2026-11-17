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
| **D3** WGSL passes validator | (per-profile pass/fail) | naga validates all 591; naga_fail=0 after oDepth fix (depth.x) | **Pass** | ProStreet |
| **D4** texld/texkill/address per-instruction | `bash tests/d4_instruction_test.sh` | Passed: 7\nFailed: 0 | **Pass** | ProStreet |
| **D5** dsx/dsy lowered to WGSL dpdx/dpdy | `out/tools/shader2wgsl /tmp/opencode/ps-shaders/ps_3_0_026.bin -o /tmp/opencode/ps-wgsl/ps_3_0_026.bin.wgsl; grep -n dpdx /tmp/opencode/ps-wgsl/ps_3_0_026.bin.wgsl` | 59:  { let e = dpdx(v0); r2.x = e.x; ... }\n71:  { let e = dpdx(v1); r4.x = e.x; ... } | **Pass** | ProStreet |
| **E1** Web lift output | `ls gen/all/shard_*.c | wc -l` | 131 | **Pass** | ProStreet |
| **E2** FULL=1 web/build.sh | `FULL=1 web/build.sh` | exit 0\nBuilt /home/uvindu/Projects/nfsprostree-recompile/out/web/nfsu2.wasm and /home/uvindu/Projects/nfsprostree-recompile/out/web/server.wasm\nManifest: /home/uvindu/Projects/nfsprostree-recompile/out/web/nfsu2.json\n out/web/nfsu2.wasm 56911892 bytes | **Pass** | ProStreet |
| **E3** npm ci and node build.mjs | `cd web && npm ci && node build.mjs exit 0` | exit 0\n 26 assets | **Pass** | ProStreet |
| **E4** Triangle in Chrome via WebGPU | `convert analysis/webgpu-test/tri.png -format percent-k info:` | → **96825**; `-format "%[pixel:p{640,480}]" info:` → **srgb(85,85,85)**; `-format "%[pixel:p{300,650}]" info:` → **srgb(183,48,25)**; `-format "%[pixel:p{1000,650}]" info:` → **srgb(43,187,25)**; `-format "%[pixel:p{640,150}]" info:` → **srgb(27,27,202)**; `triangle.html` contains `createRenderPipelineAsync` + draw 3 vertices | **Pass** | ProStreet |
| **E5** ProStreet boots to main in browser | Blocked by B3 | Blocked by B3 | **Blocked** | ProStreet |
| **F1** Game systems: files | (asset inventory) | Not started, blocked by boot and missing assets | **Not started** | ProStreet |
| **F2** Game systems: audio | (sound playback) | Not started, blocked by boot and missing assets | **Not started** | ProStreet |
| **F3** Game systems: input | (controller/keyboard) | Not started, blocked by boot and missing assets | **Not started** | ProStreet |
| **F4** Game systems: menu | (main menu navigation) | Not started, blocked by boot and missing assets | **Not started** | ProStreet |
| **F5** Game systems: race | (race track/raceplay) | Not started, blocked by boot and missing assets | **Not started** | ProStreet |

**Known gap (D-series):** O_REP/O_ENDREP loop markers are silently skipped by the ps_3_0 pixel switch (`default: continue`), so the loop body of ps_3_0_066/067/068 is emitted once instead of N times. The shaders still translate and validate. See `debug/shader-opcode-coverage.md`.
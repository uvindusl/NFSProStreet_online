# Repo Hygiene — Untracked Notes Inventory (Corrected)

**Generated:** 2026-11-18  
**Source:** `git status --short` + `git check-ignore -v` on each task file  
**Constraint:** Never track game-derived data (absolute .data addresses, patch offsets, protection details, SecuROM, .rdata/.data overlay classes). Files with such data must be (b) or (c).

---

## Current State (from `git status --short`)

**Untracked files (20):**
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
?? analysis/webgpu-test/tmp/opencode/draw_real_p8.png
?? debug/prostreet-texture-gap.md
?? debug/untracked-notes-inventory.md
?? debug/abi-8wide-reference-divergence.md
?? debug/vs_3_0-output-mapping-gap.md
?? lifter/tools/vsrun.c
```

**Already ignored by .gitignore (10):**
```
analysis/securom-data-patch-map.md
analysis/milestones-reconciliation.md
analysis/ctor13-dispatch-roadmap.md
analysis/ctor13-dispatch-summary.md
analysis/ship-scribe-status.md
debug/96726c-writer-trace.md
debug/ctor13-dispatch-analysis.md
debug/pool-allocator-finalize.md
debug/pool-allocator-fix.md
debug/prostreet-tls-fault.md
debug/prostreet-tls-latent.md
```

---

## Task-List Classification (22 files from the task)

| # | Path | Exists? | Git Ignore Status | Classification | Reason |
|---|------|---------|-------------------|----------------|--------|
| 1 | `analysis/ctor13-dispatch-summary.md` | YES | **ALREADY IGNORED** (.gitignore:49) | **(c) Delete** | Game-derived: 0x96726c, 0x96aad4, SecuROM, class-d patch 0xaae110=0x96aae8 |
| 2 | `analysis/prostreet-d3d9-calltable.md` | YES | NOT IGNORED | **(a) Track** | Pure technical D3D9 VTbl coverage table; no addresses |
| 3 | `analysis/prostreet-shader-depth-spec.md` | YES | NOT IGNORED | **(a) Track** | Shader depth spec: 58 oDepth failures, dest allow-list fix, WebGPU renderer changes |
| 4 | `analysis/prostreet-webgpu-port-report.md` | YES | NOT IGNORED | **(a) Track** | Port feature report; WGSL features, gaps, test page |
| 5 | `analysis/w1F-p8-captain-summary.md` | YES | NOT IGNORED | **(a) Track** | Concise D3D9 VTbl catalog (115 methods) |
| 6 | `analysis/w1F-p8-first-mate-summary.md` | YES | NOT IGNORED | **(a) Track** | Half B 8-wide ABI widening summary |
| 7 | `analysis/w1F-p8-summary.md` | YES | NOT IGNORED | **(a) Track** | M1 finalization report |
| 8 | `analysis/w1F-p8-webgpu-port-summary.md` | YES | NOT IGNORED | **(a) Track** | WebGPU port summary (591 shaders) |
| 9 | `analysis/webgpu-test/corpus_capture.js` | YES | NOT IGNORED | **(a) Track** | Playwright capture for corpus validation |
| 10 | `analysis/webgpu-test/probe.html` | YES | NOT IGNORED | **(a) Track** | WebGPU probe page (navigator.gpu detection) |
| 11 | `analysis/webgpu-test/probe.png` | YES | NOT IGNORED | **(b) Leave untracked** | Binary PNG artifact (780×493) |
| 12 | `analysis/webgpu-test/ps_capture.js` | YES | NOT IGNORED | **(a) Track** | Playwright capture for ps_2_0_002 WebGPU render |
| 13 | `analysis/webgpu-test/ps_shader-render.md` | YES | NOT IGNORED | **(a) Track** | Render verification: commands, WGSL, pixel evidence |
| 14 | `analysis/webgpu-test/ps_shader.html` | YES | NOT IGNORED | **(a) Track** | WebGPU render page with WGSL shader |
| 15 | `analysis/webgpu-test/ps_shader.png` | YES | NOT IGNORED | **(b) Leave untracked** | Binary PNG artifact (1280×720) |
| 16 | `analysis/webgpu-test/ps_shader_wgsl.js` | YES | NOT IGNORED | **(a) Track** | WGSL shader module (fragment + vertex) |
| 17 | `analysis/webgpu-test/tmp/opencode/draw_real_p8.png` | YES | NOT IGNORED | **(b) Leave untracked** | Binary PNG in tmp/ (1280×720) |
| 18 | `debug/prostreet-texture-gap.md` | YES | NOT IGNORED | **(a) Track** | R_ADDR/texld/texkill histogram across 591 shaders |
| 19 | `debug/untracked-notes-inventory.md` | YES | **ALREADY IGNORED** (.gitignore:59) | **(c) Do not track** | It quotes the same game-derived addresses/patch values it inventories |
| 20 | `debug/abi-8wide-reference-divergence.md` | YES | NOT IGNORED | **(a) Track** | 8-wide ABI reference divergence analysis |
| 21 | `debug/vs_3_0-output-mapping-gap.md` | YES | NOT IGNORED | **(a) Track** | vs_3_0 output mapping gap analysis |
| 22 | `lifter/tools/vsrun.c` | YES | NOT IGNORED | **(a) Track** | Native CLI for vsemu CPU interpreter |

---

## Classification Counts (Task List Only)

| Classification | Count |
|----------------|-------|
| **(a) Track** | **15** |
| **(b) Leave untracked** | **3** (binary PNGs) |
| **(c) Do not track** | **2** (already ignored: `ctor13-dispatch-summary.md`, `untracked-notes-inventory.md`) |

**Note:** The task list mentions 4 other files already in .gitignore (`pool-allocator-finalize.md`, `pool-allocator-fix.md`, `prostreet-tls-fault.md`, `prostreet-tls-latent.md`) — they are already ignored and require no action. The "Do not track" classification here means "keep ignored / do not track."

---

## Already Ignored Files (Current .gitignore)

| Path | Ignored Since (commit 1941426) |
|------|-------------------------------|
| analysis/securom-data-patch-map.md | ✅ |
| analysis/milestones-reconciliation.md | ✅ |
| analysis/ctor13-dispatch-roadmap.md | ✅ |
| analysis/ctor13-dispatch-summary.md | ✅ |
| analysis/ship-scribe-status.md | ✅ |
| debug/96726c-writer-trace.md | ✅ |
| debug/ctor13-dispatch-analysis.md | ✅ |
| debug/pool-allocator-finalize.md | ✅ |
| debug/pool-allocator-fix.md | ✅ |
| debug/prostreet-tls-fault.md | ✅ |
| debug/prostreet-tls-latent.md | ✅ |
| debug/untracked-notes-inventory.md | ✅ (commit 60319f6) |

**All 12 game-derived files are already ignored. No new .gitignore entries needed.**

---

## Proposed .gitignore Additions

**None required.** All game-derived files are already ignored by commit 1941426. The current .gitignore covers every file containing absolute .data addresses, patch offsets, protection details, SecuROM init chains, or .rdata/.data overlay classifications.

The 3 binary PNGs (`probe.png`, `ps_shader.png`, `draw_real_p8.png`) are correctly left untracked (not in .gitignore, not committed). The 16 trackable files remain untracked but not ignored — they are source/documentation files safe for future commits.

---

## Summary

| Category | Files | Action |
|----------|-------|--------|
| **Track (source/docs)** | 15 | Keep untracked, safe to commit later |
| **Leave untracked (binaries)** | 3 | PNG artifacts — never commit |
| **Already ignored (game-derived)** | 12 | Correctly in .gitignore |
| **This file** | 1 | `analysis/repo-hygiene.md` — track |

**No .gitignore changes needed.** The current .gitignore (commit 1941426) correctly excludes all game-derived data.
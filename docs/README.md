# Documentation Index

> **Start here:** [`milestones.md`](milestones.md) has the definitive A–F checklist of what works and what doesn't.
> **Roadmap:** [`ROADMAP.md`](ROADMAP.md) has the phased plan to get the game running natively on Linux and in the browser.

## Design & Architecture

| Document | What it covers |
|---|---|
| [`phase1-reference-notes.md`](phase1-reference-notes.md) | Study notes from the nfsu2-web reference project |
| [`phase2-function-map.md`](phase2-function-map.md) | Binary analysis: function discovery, import tables, relocations |
| [`phase3-lifter.md`](phase3-lifter.md) | The x86 → C lifter: how it works, the ABI, edge cases |
| [`web-build-and-hle-port-guide.md`](web-build-and-hle-port-guide.md) | WebAssembly / WebGPU build pipeline and HLE porting steps |

## Status & Coverage

| Document | What it covers |
|---|---|
| [`milestones.md`](milestones.md) | **Master checklist** — every milestone with proof commands and pass/fail |
| [`ROADMAP.md`](ROADMAP.md) | **Roadmap** — phased plan from current state to playable game (native Linux + browser) |
| [`boot-baseline-verification.md`](boot-baseline-verification.md) | Independent re-verification of the boot sequence |
| [`import-coverage.md`](import-coverage.md) | Win32 / CRT / D3D9 import coverage report (371 imports, 15 DLLs) |
| [`d3dx-coverage.md`](d3dx-coverage.md) | D3D9 / D3DX API coverage: what's stubbed vs. implemented |

## Historical (Superseded)

| Document | Note |
|---|---|
| [`browser-port-action-plan.md`](browser-port-action-plan.md) | ⚠️ Superseded by `milestones.md` — kept for history |

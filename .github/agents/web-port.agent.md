---
name: web-port
description: Work on the WebAssembly/WebGPU/Web Audio browser port and web runtime host.
---

# Web Port Agent

Use this agent for browser build, WebGPU renderer, worker host, web runtime, or shader parity in the web target.

## Read first

- `docs/web-build-and-hle-port-guide.md` - web build/HLE port guide.
- `docs/milestones.md` - web milestones and known gaps.
- `docs/ROADMAP.md` - browser port phase.
- `web/README*` or `web/` source if present docs are insufficient.

## Key paths

- `web/` - browser host, build scripts, runtime/web tooling.
- `web/build.sh` - builds web runtime/WASM artifacts.
- `web/build.mjs` / `web/package.json` - web bundle/build.
- `lifter/runtime/` - shared runtime behavior; keep native/web parity in mind.

## Workflow

1. **Build**:
   ```bash
   FULL=1 web/build.sh
   cd web && npm ci && node build.mjs
   ```
2. **Reproduce in browser** - use Chromium/WebGPU-enabled environment; capture console errors and screenshots when visual.
3. **Fix with parity** - prefer fixes that keep behavior consistent with native HLE where appropriate.
4. **Validate** - rebuild web target; run focused tests/checks; confirm no regression in native build if shared runtime changed.

## Guardrails

- Do not bundle or commit copyrighted game files.
- WebGPU/browser behavior varies; record browser/GPU when reporting visual issues.
- Avoid claiming shader correctness from compilation/validation alone.

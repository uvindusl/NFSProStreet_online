# Web Port Skill

## When to use

WASM build, WebGPU renderer, Web Audio, browser host, or native/web parity work.

## Inputs

- Browser console/log/screenshot.
- Web build output.
- Related runtime/HLE code.

## Procedure

1. Build: `FULL=1 web/build.sh && (cd web && npm ci && node build.mjs)`.
2. Reproduce in Chromium/WebGPU-capable environment.
3. Fix smallest layer causing issue; keep native parity where semantics are shared.
4. Verify web build and, if shared code changed, native `STRICT=1 scripts/build_runtime.sh`.

## Output checklist

- Browser/GPU/environment used.
- Before/after behavior.
- Verification commands.
- Remaining browser-specific risks.

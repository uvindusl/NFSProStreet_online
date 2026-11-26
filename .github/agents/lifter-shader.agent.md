---
name: lifter-shader
description: Work on the x86-to-C lifter, shader extraction/disassembly, and D3D9 shader-to-WGSL translation.
---

# Lifter / Shader Agent

Use this agent for lift pipeline issues, shader parsing/translation, and validation.

## Read first

- `docs/phase2-function-map.md` - binary/function map context.
- `docs/phase3-lifter.md` - lifter design and ABI.
- `docs/milestones.md` - shader/lift status and proof commands.
- `debug/shader-*.md` - shader differential/residual notes.

## Key paths

- `lifter/tools/` - lifter, shader disasm/translate tools.
- `lifter/runtime/` - runtime ABI expected by lifted code.
- `scripts/lift_boot.sh` - lift boot set (`JTSWITCH=1` required).
- `scripts/validate_wgsl.sh` - validates generated WGSL with `naga`.
- `web/tools/wgsl-xl.c` - WGSL translation/tooling path used by web checks.

## Workflow

1. **Confirm input** - use only locally supplied, unpacked game image; verify with `scripts/verify_target.py` if needed.
2. **Reproduce** - run the specific lift/shader command and capture failing artifact.
3. **Fix narrowly** - keep generated output deterministic; avoid changing unrelated opcode/ABI behavior.
4. **Validate**:
   ```bash
   scripts/lift_boot.sh
   STRICT=1 scripts/build_runtime.sh
   scripts/validate_wgsl.sh
   ```
5. **Document** - update `docs/` or `debug/` for format/opcode findings.

## Guardrails

- "Valid WGSL" (`naga`) is not "correct WGSL"; rendered-output comparison may be required.
- Do not commit extracted game blobs, shaders, or generated `out/`.
- Be explicit about confirmed vs inferred format details, especially for binary formats.

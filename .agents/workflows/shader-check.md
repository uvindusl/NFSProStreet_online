# Shader / WGSL Check Workflow

Use when changing shader extraction, disassembly, translation, or WebGPU shader behavior.

## 1. Reproduce

Identify affected shader IDs/families and capture current output.

```bash
scripts/validate_wgsl.sh
```

For specific tools/tests, use targeted scripts in `scripts/` or `tests/` (e.g. `tests/d4_instruction_test.sh`).

## 2. Inspect

- Check translator/disassembler in `lifter/tools/` and `web/tools/wgsl-xl.c`.
- Compare against D3D9 bytecode/reference behavior when possible.
- Treat `naga` validation as syntax/validity, not correctness.

## 3. Fix

- Handle the specific opcode/format case.
- Keep behavior explicit; refuse/diagnose unsupported cases rather than silently skipping.
- Document inferred format details clearly.

## 4. Verify

```bash
scripts/validate_wgsl.sh
STRICT=1 scripts/build_runtime.sh   # if shared/runtime touched
```

For visual correctness, run the relevant WebGPU/native test and capture evidence (screenshot/pixel diff/log). Note GPU/browser for WebGPU results.

## 5. Record

Update `debug/shader-*.md` or docs with confirmed behavior, remaining unsupported cases, and proof commands.

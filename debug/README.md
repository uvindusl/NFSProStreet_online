# Debug Notes Index

Working notes from debug sessions. These are raw investigation logs — for the
canonical project status, see [`docs/milestones.md`](../docs/milestones.md).

## Boot & Runtime

| File | Topic |
|---|---|
| `ctor13-dispatch-analysis.md` | Static constructor 13 — the current boot blocker |
| `pool-allocator-fix.md` | Pool allocator crash fix |
| `pool-allocator-finalize.md` | Pool allocator finalisation |
| `prostreet-tls-fault.md` | Thread-local storage fault analysis |
| `prostreet-tls-latent.md` | Latent TLS issues |
| `96726c-writer-trace.md` | Writer trace at address 0x96726c |

## Shaders — Pipeline & Validation

| File | Topic |
|---|---|
| `prostreet-shader-inventory.md` | Shader blob inventory (591 blobs) |
| `prostreet-shader-corpus.md` | Corpus-level shader analysis |
| `prostreet-shader-capture.md` | Shader capture methodology |
| `prostreet-wgsl-validation.md` | WGSL validation results |
| `prostreet-wgsl-validator.md` | Validator setup and usage |
| `shader-opcode-coverage.md` | D3D9 opcode coverage in the WGSL emitter |
| `shader-emitter-semantics.md` | Emitter semantic correctness notes |
| `shader-sample-verification.md` | Sample-level shader output verification |
| `shader-differential.md` | Cross-path shader differential (CPU vs WGSL vs MSL) |
| `vertex-path-differential.md` | Vertex shader path differential |
| `pixel-path-differential.md` | Pixel shader path differential |
| `shader-residual-classes.md` | Residual shader opcode classes |
| `metal-differential-results.md` | Shader differential on Apple Metal (M2 Pro) |
| `class-d-analysis.md` | Class D (shader instruction) analysis |

## Shaders — 8-Wide ABI (Inter-Stage Widening)

| File | Topic |
|---|---|
| `abi-8wide-summary.md` | **Start here** — synthesis of the 8-wide ABI work |
| `abi-8wide-design.md` | Design: 4→8 texture stage widening |
| `abi-8wide-coverage.md` | Coverage analysis |
| `abi-8wide-corpus-counts.md` | Corpus dcl counts |
| `abi-8wide-corpus-spotcheck.md` | Corpus spot-check |
| `abi-8wide-ot8.md` | oT8+ output register handling |
| `abi-8wide-ot8-evidence.md` | Evidence for oT8+ behaviour |
| `abi-8wide-remaining-4wide.md` | Remaining 4-wide paths |
| `abi-8wide-mirror-vsemu.md` | vsemu mirror sync |
| `abi-8wide-leftover-audit.md` | Leftover 4-wide audit |
| `abi-8wide-encoder-smoke.md` | Encoder smoke test |
| `abi-8wide-encoder-smoke-audit.md` | Encoder smoke test audit |
| `abi-8wide-encoder-real-smoke.md` | Real encoder smoke test |
| `abi-8wide-reverify-d35e7a2.md` | Re-verification at commit d35e7a2 |
| `abi-8wide-harness-coverage.md` | Harness coverage |
| `abi-8wide-proto-coverage.md` | Proto coverage |
| `abi-8wide-reference-divergence.md` | Reference divergence notes |

## Build & Regression

| File | Topic |
|---|---|
| `prostreet-build-verify.md` | Build verification log |
| `prostreet-regression.md` | Regression tracking |
| `prostreet-disasm-opcodes.md` | Disassembler opcode coverage |
| `prostreet-disasm-texld-fix.md` | texld disassembly fix |
| `prostreet-d4-instruction-tests.md` | D4 instruction test results |
| `prostreet-texture-gap.md` | Texture implementation gap |
| `prostreet-texture-runtime-fix.md` | Texture runtime fix |
| `tracked-asset-audit.md` | Tracked asset audit |
| `untracked-notes-inventory.md` | Inventory of untracked notes |
| `vs_3_0-output-mapping-gap.md` | vs_3_0 output register mapping gap |

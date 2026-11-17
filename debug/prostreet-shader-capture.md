# ProStreet Shader Capture — Corrected Mechanism

## Goal

Document how real D3D9 shader bytecode is stored and how it was captured, and honestly state what cannot be done here (live runtime capture).

## Corrected mechanism — shaders are stored as compiled D3D9 bytecode, NOT compiled from HLSL at runtime

Earlier assumption (proven wrong): shaders are compiled at runtime from HLSL.

Correct finding: the shaders are precompiled and stored as raw D3D9 tokenized bytecode in the clean retail image's `.rsrc` section. The unpacked artifact `artifacts/nfs.exe.unpacked` has its `.rsrc` section 100 % zeroed (the unpacker did not carry resources over), which is why an earlier scan of the unpacked image found nothing. The clean image at
`/home/uvindu/.local/share/Trash/files/Need for Speed - ProStreet/nfsps.exe` has:

| Item | Value |
|------|-------|
| Section | `.rsrc` |
| RVA | `0xc11000` |
| Size | `0xd9000` bytes (~889 KB) |
| Nonzero content | ~45.3 % |

## Storage format (per shader blob)

```
DWORD length          ; byte length of the following bytecode
BYTE  bytecode[length] ; D3D9 token stream
```

D3D9 token stream layout:
1. Version token (`VS 0xFFFE0000 \| major<<8 \| minor`, `PS 0xFFFF0000 \| major<<8 \| minor`)
2. CTAB comment
3. PRES comment
4. Instruction stream
5. END token `0x0000FFFF`

## Extraction (ran, blobs NOT in repo)

Command:
```
python3 lifter/tools/extract_shaders.py <PE-IMAGE> <OUTDIR>
```

Ran against the clean retail image:
```
python3 lifter/tools/extract_shaders.py \
  /home/uvindu/.local/share/Trash/files/Need for Speed - ProStreet/nfsps.exe \
  /tmp/opencode/ps-shaders
```

Result: **591 shaders**, all in `.rsrc`, all with CTAB + PRES, written to `/tmp/opencode/ps-shaders/` (NOT the repo — no game assets copied into the repo).

Breakdown by profile:
| Profile | Count | Min size | Max size | Total bytes |
|---------|-------|----------|----------|-------------|
| ps_2_0 | 223 | 144 | 2636 | 98,172 |
| vs_2_0 | 199 | 144 | 5264 | 138,044 |
| ps_3_0 | 71 | 412 | 5648 | 201,552 |
| vs_3_0 | 71 | 648 | 4236 | 127,620 |
| vs_1_1 | 27 | 144 | 3468 | 19,052 |
| **Total** | **591** | **144** | **5648** | **584,440** |

## Translation validation

Reference parser + emitter `out/tools/shader2wgsl` (built by `lifter/tools/build_shader2wgsl.sh`):

| Result | Count | Notes |
|--------|-------|-------|
| Translate to WGSL | 533 | OK |
| Fail to translate | 58 | **All ps_2_0**; all write `oDepth` (register type `R_DEPTHOUT`), which the emitter rejects |

## What remains impossible here (honest note)

Live runtime capture is still not possible in this environment:

- **Wine** is not installed (`wine`/`wineserver`/`wine64` all missing; no `apt`/`apt-get`), so the retail `nfs.exe` cannot run under Wine with a D3D9 proxy/DLL override logging `CreateVertexShader`/`CreatePixelShader` bytecode.
- The web runtime (`web/runtime/web/shader_dump.c`, gated by `NFSU2_SHADER_DUMP`) does write raw D3D9 bytecode blobs, but `web/` has no build target that runs the game, and the headless lifter boot stops at **ctor-13** (`unresolved indirect call to 0x0096aad4`, per `out/boot-m1b.log`) before any D3D9 device creation.
- `lifter/runtime/d3d9_hle.c` `dev_CreateVertexShader`/`dev_CreatePixelShader` are stubs that discard `pFunction` (bytecode) — no capture implemented there either.

So the only achievable capture path here is static extraction from `.rsrc` (done; blobs in `/tmp/opencode/ps-shaders`). No live runtime shader-creation call ever occurs in this environment.

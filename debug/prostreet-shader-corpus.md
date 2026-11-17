# ProStreet Shader Corpus — Format, Table, and Reproduction

## Container format

Shader bytecode is stored as compiled D3D9 tokenized blobs inside the clean retail image's `.rsrc` section:

| Item | Value |
|------|-------|
| Image | `/home/uvindu/.local/share/Trash/files/Need for Speed - ProStreet/nfsps.exe` (clean retail, DRM-free) |
| Section | `.rsrc` |
| RVA | `0xc11000` |
| File offset | `0xc11000` (matches RVA) |
| Size | `0xd9000` bytes (~889 KB) |
| Nonzero content | ~45.3 % |

Per-shader blob layout inside `.rsrc` (extracted by `lifter/tools/extract_shaders.py`):

```
DWORD length          ; byte length of the following bytecode
BYTE  bytecode[length] ; D3D9 token stream
```

D3D9 token stream layout:
1. Version token — VS: `0xFFFE0000 \| major<<8 \| minor`; PS: `0xFFFF0000 \| major<<8 \| minor`
2. CTAB comment
3. PRES comment
4. Instruction stream
5. END token `0x0000FFFF`

All 591 blobs have both CTAB and PRES comments.

## Shader table by profile

| Profile | Version token | Count | Min size | Max size | Total bytes |
|---------|---------------|-------|----------|----------|-------------|
| ps_2_0 | `0xFFFF0200` | 223 | 144 | 2636 | 98,172 |
| vs_2_0 | `0xFFFE0200` | 199 | 144 | 5264 | 138,044 |
| ps_3_0 | `0xFFFF0300` | 71 | 412 | 5648 | 201,552 |
| vs_3_0 | `0xFFFE0300` | 71 | 648 | 4236 | 127,620 |
| vs_1_1 | `0xFFFE0101` | 27 | 144 | 3468 | 19,052 |
| **Total** | — | **591** | **144** | **5648** | **584,440** |

All shards live in `.rsrc`; no shaders found in `.text`, `.data`, or `.rdata` of either the clean image or the unpacked artifact (the unpacker zeroed `.rsrc`, which is why the unpacked-image scan found nothing).

## Exact commands to reproduce

1. Extract blobs from the clean retail image:
   ```
   python3 lifter/tools/extract_shaders.py \
     /home/uvindu/.local/share/Trash/files/Need for Speed - ProStreet/nfsps.exe \
     /tmp/opencode/ps-shaders
   ```
   Writes 591 blobs (`ps_2_0_NNN.bin`, `vs_2_0_NNN.bin`, etc.) to `/tmp/opencode/ps-shaders/`.

2. Build the reference shader→WGSL translator:
   ```
   lifter/tools/build_shader2wgsl.sh   # produces out/tools/shader2wgsl
   ```

3. Validate / translate (optional, requires running `out/tools/shader2wgsl` on the blobs).

## The 58-shader oDepth gap

Validation with `out/tools/shader2wgsl`:
- **533 of 591** translate to WGSL.
- **58 fail** to translate — **all are `ps_2_0`**, and every failure writes `oDepth` (register type `R_DEPTHOUT`), which the WGSL emitter rejects.

No `vs_*`, `ps_3_0`, or `vs_3_0` failures. The gap is isolated to depth-writing pixel shaders on the `ps_2_0` profile.

## Repo hygiene

No game assets copied into the repo. All extracted blobs live at `/tmp/opencode/ps-shaders/` only. This document and `debug/prostreet-shader-capture.md` are the only repo artifacts.

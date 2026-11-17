# ProStreet Shader Inventory

**Source:** retail install at `/home/uvindu/Documents/Games/Need for Speed ProStreet/` (read-only; no assets copied or committed).
**Scope:** static filesystem scan only — no shader bytecode was extracted from package archives.

## 1. Shader format: D3D9 tokenized bytecode, NOT DXBC

The project's D3D9 HLE runtime (`lifter/runtime/d3d9_hle.c`, `lifter/runtime/d3d9_hle.h`) and the shader dump helper (`web/runtime/web/shader_dump.c`) show the game creates shaders through `CreateVertexShader` / `CreatePixelShader` with D3D9 tokenized bytecode, consumed by `vs_disasm`/`vs_version`/`vs_ninstr`. `shader_dump.c` writes `vsobj_<n>.bin` / `psobj_<id>.bin` (raw bytecode) plus `.asm` disassembly.

**DXBC check:** scanned every non-media file in the 10 GB install for the `DXBC` magic (`0x44584243`) — **zero hits**. Shader containers are therefore **not DXBC** (i.e. not D3D10/D3D11-style containers). The retail game ships D3D9-era shaders.

## 2. Discrete shader files on disk: none found

- No files named `*shader*`, `vs_*`, `ps_*`, `*d3d*`, `*fxc*` outside `SOUND/` (the `ps_*` matches there are audio: `ps_whooshes.bnk`, `ps_music.*`, `ps_nis_pc_*.sbk/rbk` — unrelated to graphics).
- The 15 `*.fx` files under `SOUND/FXEDIT/` are audio-effect presets, not Direct3D effects.
- `*.mtl` files (e.g. `CARS/DAMAGE/attachments.mtl`) are plain Wavefront-style material/physics params, no shader bytecode.

## 3. Candidate shader-storage packages (undecoded)

The D3D9 shader bytecode is not present as clean, enumerable loose files. It is either inside proprietary/compressed EA packages or compiled at runtime. Candidate containers scanned:

| File | Size | Notes |
|------|------|-------|
| `GLOBAL/GLOBALB.BUN` | ~12.4 MB | EA bundle; shader-version-like DWORDs present (97 PS-pattern hits) but **not validated** as clean shaders (e.g. `0xFFFF0209` is not a valid PS version; `0xFFFFxxxx` also appears in colors/coordinates) |
| `GLOBAL/GlobalMemoryFile.bin` | ~9.5 MB | 1 VS + 11 PS-pattern hits; first hit `0xFFFF0100` (ps_1_0) but following dwords look random/encrypted — unvalidated |
| `GLOBAL/DYNTEX.BIN` | ~46.6 MB | 13 PS-pattern hits; unvalidated |
| `CARS/*/GEOMETRY.BIN` | ~18–34 MB | 1 VS + ~5 PS-pattern hits (Eclipse); unvalidated |
| `GLOBAL/ATTRIBUTES.BIN` (VPAK) | ~3.2 MB | Parsed directory — contains camera/attribute names (`cam1`, `Cam*Bck`, `Debug`, `debg`, `car`), **not shaders** |
| `*.bbg` (per-car damage packages) | ~0.6 MB each | Header `00 0f 01 00 ...`; undecoded, no shader hits in samples |
| `*.lzc` (gameplay.lzc, GlobalB.lzc, InGameB.lzc) | — | Compressed packages; not decompressed/scanned |

**None of these candidates could be confirmed as containing clean, enumerable vertex/pixel shader blobs.** The version-token hits are ambiguous (colors/coordinates/coincidences produce `0xFFFFxxxx` and `0xFFFExxxx` patterns), and the packages are compressed or use an undecoded EA container format.

## 4. Counts / sizes / shader-model versions

**Not enumerable from static scan.** No validated shader blob table could be produced because:
- No DXBC containers to parse.
- No validated D3D9 version tokens followed by intact instruction streams found outside ambiguous package data.
- Package containers (`.BUN`, `.bbg`, `.lzc`, VPAK) not reverse-engineered in this session.

If shader bytecode is required, the supported capture path is runtime interception: set `NFSU2_SHADER_DUMP=<dir>` (per `shader_dump.c`) so the web/D3D9 HLE writes every `CreateVertexShader`/`CreatePixelShader` bytecode blob (`vsobj_*.bin`, `psobj_*.bin`) plus disassembly, with shader-model version and instruction count logged to `index.txt`.

## 5. Bottom line

- Container/format in retail install: **not DXBC**; expected D3D9 tokenized shader bytecode (per HLE runtime), but **no clean on-disk shader blobs found**.
- Vertex shader count / Pixel shader count / sizes / shader-model versions: **undetermined** (stored in undecoded/compressed packages or generated at runtime).
- Recommended next step: capture shaders at runtime via `NFSU2_SHADER_DUMP` rather than reverse-engineering `.BUN`/`.bbg`/`.lzc` containers.

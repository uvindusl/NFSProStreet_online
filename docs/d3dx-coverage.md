# D3D9 / D3DX Coverage Report

**Source data:** `out/imports-runtime-new.json` (14 `D3DX*` imports from `d3dx9_34.dll`), `reference/nfsu2-web/runtime/d3dx_hle.c` (full HLE reference), and `lifter/runtime/win32.c` (ProStreet implementation).
**Goal:** Identify what ProStreet calls, what we do today, what the reference does, and the concrete gap to a first rendered frame.

## 1. What ProStreet calls (from `out/imports-runtime-new.json`)

| # | Import name | DLL | `win32_call` handling |
|---|---|---|---|
| 1 | `D3DXCreateEffectFromResourceA` | `d3dx9_34.dll` | Returns `r = 0` (unimplemented) |
| 2 | `D3DXMatrixMultiplyTranspose` | `d3dx9_34.dll` | Returns `r = 0` (unimplemented) |
| 3 | `D3DXCreateEffectPool` | `d3dx9_34.dll` | Returns `r = 0` (unimplemented) |
| 4 | `D3DXSaveSurfaceToFileA` | `d3dx9_34.dll` | Returns `r = 0` (unimplemented) |
| 5 | `D3DXGetVertexShaderProfile` | `d3dx9_34.dll` | Returns `r = 0` (unimplemented) |
| 6 | `D3DXGetPixelShaderProfile` | `d3dx9_34.dll` | Returns `r = 0` (unimplemented) |
| 7 | `D3DXVec3Transform` | `d3dx9_34.dll` | Returns `r = 0` (unimplemented) |
| 8 | `D3DXVec4Transform` | `d3dx9_34.dll` | Returns `r = 0` (unimplemented) |
| 9 | `D3DXMatrixMultiply` | `d3dx9_34.dll` | Returns `r = 0` (unimplemented) |
| 10 | `D3DXSaveSurfaceToFileInMemory` | `d3dx9_34.dll` | Returns `r = 0` (unimplemented) |
| 11 | `D3DXCreateCubeTextureFromFileInMemory` | `d3dx9_34.dll` | Returns `r = 0` (unimplemented) |
| 12 | `D3DXCreateVolumeTextureFromFileInMemory` | `d3dx9_34.dll` | Returns `r = 0` (unimplemented) |
| 13 | `D3DXMatrixTranspose` | `d3dx9_34.dll` | Returns `r = 0` (unimplemented) |
| 14 | `D3DXMatrixInverse` | `d3dx9_34.dll` | Returns `r = 0` (unimplemented) |

All 14 map to the `strncmp(n, "D3DX", 4) == 0` case in `win32_call()` (line ~1680), which returns `r = 0` and triggers the `[unimplemented]` log.

## 2. What the reference (NFSU2) does

The reference `nfsu2-web` has a full D3DX9 HLE implementation in `reference/nfsu2-web/runtime/d3dx_hle.c`. Key aspects:

- **`sub_00640c02`** (`D3DXCreateEffectFromResourceA`): Creates an `ID3DXEffect` stub object, records the effect resource, and returns a fake effect pointer (`0x10000000`).
- **Effect pass system:** Tracks passes via `D3DXSTAT` / `D3DXANALYZE` environment variables. Maintains counters for `BeginPass`, `CommitChanges`, `SetMatrix`, `SetVector`, `SetTexture`, etc.
- **`D3DXVec3Transform` / `D3DXVec4Transform`:** Vector/matrix transform operations implemented as C math.
- **`D3DXMatrixMultiply` / `D3DXMatrixMultiplyTranspose`:** Matrix multiply intrinsics.
- **`D3DXSaveSurfaceToFileA` / `D3DXSaveSurfaceToFileInMemory`:** Surface save stubs that write PPM/PGM-ish data to a guest-file buffer.
- **`D3DXCreateCubeTextureFromFileInMemory` / `D3DXCreateVolumeTextureFromFileInMemory`:** Cube/volume texture stubs that return fake texture handles.
- **State memoization:** `d3dx_hle.c` maintains `g_dx[DX_N]` counters (BeginPass, PassApply, StateApply, EvalState, VM, Gather, etc.) measurement.
- **`ID3DXEffect::BeginPass` / `CommitChanges`:** The core of the effect framework — these are where the game evaluates parameter expressions and sets device state. Measurement shows ~48% of the guest thread in a race frame is inside `BeginPass`.

## 3. Gap analysis: ProStreet vs. Reference

| ProStreet (today) | Reference (NFSU2) | Gap / Impact |
|---|---|---|
| `D3DXCreateEffectFromResourceA` → `r = 0` | Creates effect stub, returns handle | **Cannot create D3DX effect** — game will fail at effect init. |
| All other D3DX imports → `r = 0` | Full HLE (vector/matrix transforms, texture creation, surface saving) | **No D3DX math, no texture loading, no effect parameter evaluation.** |
| `Direct3DCreate9` → `r = 0` (see boot-baseline-verification) | Creates `IDirect3D9` device stub | **Cannot create D3D9 device** — renders nothing. |
| No `d3dx_hle.c` equivalent | `g_dx[]` counters, beginpass/commit/state tracking | **No effect pass evaluation** — the game's shader/parameter setup breaks. |
| `IDirect3DDevice9` methods not in import table | Tracked in `com.c` (36k+ calls/frame) | **Device call path missing entirely.** |

## 4. Concrete path to first rendered frame

1. **`Direct3DCreate9()`** must return a valid `IDirect3D9` pointer. Currently returns 0 → game cannot create a device.
2. **`D3DXCreateEffectFromResourceA()`** must create an effect object. Currently returns 0 → effect framework never starts.
3. **`ID3DXEffect::BeginPass()`** — once an effect exists, the game evaluates parameter expressions. Reference has full state tracking (preshaders, gather inputs, eval VM). ProStreet has nothing.
4. **`D3DXMatrixMultiply` / `D3DXVec*`** — used inside effect parameter expressions. Without these, expression evaluation produces garbage or skips.
5. **Device state setting** (`SetMatrix`, `SetTexture`, `SetVector`) — referenced in `com.c` as 36k+ calls/frame. These must be HLE'd.

**Minimum to get a frame:** Implement `Direct3DCreate9` → return stub `IDirect3D9`, implement `D3DXCreateEffectFromResourceA` → return stub `ID3DXEffect`, implement `BeginPass`/`CommitChanges` stubs, and a minimal `D3DXVec3Transform` / `D3DXMatrixMultiply`.

## 5. Knowns / Unknowns / Next steps

- **Known:** All 14 D3DX imports are unimplemented in ProStreet. Reference has full HLE.
- **Known:** `Direct3DCreate9` returns 0; the game cannot proceed to D3D9 device creation.
- **Unknown:** Exact order of imports the game calls during init (needs live trace or IDA disassembly of the decrypted image).
- **Unknown:** Whether the game uses `D3DXCreateEffectFromResourceA` with a embedded resource or with a file path.
- **Next:** Implement `Direct3DCreate9` returning a stub device, then `D3DXCreateEffectFromResourceA`, then the `BeginPass`/`CommitChanges` / expression evaluation path from the reference.

## 6. Reference URLs / Cross-references

- `reference/nfsu2-web/runtime/d3dx_hle.c` — full D3DX9 HLE, measurement, and counters.
- `reference/nfsu2-web/runtime/com.c` — IDirect3DDevice9 method tracking (COMSTAT).
- `lifter/runtime/win32.c` lines ~1680: `strncmp(n, "D3DX", 4) == 0` case.
- `out/imports-runtime-new.json` — import list confirming 14 D3DX entries.
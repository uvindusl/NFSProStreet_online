# ProStreet texture runtime fix — LockRect/UnlockRect segfault

## Symptom

`tests/d3d9_hle_textured_quad.c` segfaulted on `LockRect`/`UnlockRect`:

```
[d3d9] IDirect3DTexture9::LockRect(Level=0, Flags=0)
[d3d9] IDirect3DTexture9::UnlockRect
[SEGFAULT at memcpy(locked.pBits, g_checker, 16)]
```

The test creates a 2×2 `D3DFMT_X8R8G8B8` texture, locks level 0, copies 16 bytes of checkerboard pixel data into `locked.pBits`, unlocks, sets the texture on stage 0, and draws a quad. `locked.pBits` was NULL → `memcpy(NULL, ...)` → SIGSEGV.

## Root cause

Three gaps in `lifter/runtime/d3d9_hle.c`:

1. **`TextureImpl` had no pixel storage.** The struct (`d3d9_hle.c:216`) stored only `width, height, levels, usage, format, pool` — no `data` pointer for the texture image.

2. **`tex_LockRect` was a no-op stub** (`d3d9_hle.c:587`). It accepted `pLockedRect` but never wrote `pLockedRect->pBits` or `pLockedRect->Pitch`, so `locked.pBits` remained NULL.

3. **`tex_UnlockRect` was a no-op stub** (`d3d9_hle.c:593`). It returned `D3D_OK` without any state tracking.

This is a texture state tracking gap in the HLE runtime — the runtime accepted `CreateTexture` and `SetTexture` calls but never allocated or exposed texture memory for CPU upload via `LockRect`.

## Fix (minimal, in `lifter/runtime/d3d9_hle.c`)

1. **Added `void* data` field to `TextureImpl`** — holds the level-0 image pixels (allocated lazily on first `LockRect`).

2. **Implemented `tex_LockRect`** — allocates `width * height * 4` bytes (X8R8G8B8 = 4 bytes/pixel) on first lock, returns `pLockedRect->pBits = impl->data` and `pLockedRect->Pitch = width * 4`. Subsequent locks return the same buffer.

3. **`tex_UnlockRect`** — kept as a no-op returning `D3D_OK` (no dirty-region tracking needed for the HLE).

4. **`tex_Release`** — frees `impl->data` when the last reference is released, preventing a leak.

All changes are in `lifter/runtime/d3d9_hle.c` only — no header changes needed (`D3DLOCKED_RECT` and `TextureImpl` were already defined/used internally).

## Verification

Build and run:
```
bash tests/build_d3d9_textured_quad.sh
```

Result: **ALL TESTS PASSED** — all 16 steps succeed, including:
- Step 11: `LockRect` returns non-NULL `pBits`, `memcpy` of checkerboard succeeds
- Step 12: `SetTexture(0, tex)` tracks texture in `device->textures[0]`
- Step 13: `DrawPrimitiveUP` with bound texture succeeds
- Step 16: `Release` frees texture data without leak

Console output confirms `LockRect` → `memcpy` → `UnlockRect` → `SetTexture` → `DrawPrimitiveUP` → `EndScene` → `Present` all return `D3D_OK`.

## Remaining limitations

- `LockRect` assumes `D3DFMT_X8R8G8B8` (4 bytes/pixel). Other formats would need a bytes-per-pixel lookup keyed on `impl->format`.
- `LockRect` doesn't honor `pRect` (sub-rectangle locking) or `Flags` (`D3DLOCK_DISCARD`, `D3DLOCK_READONLY`, etc.) — acceptable for the HLE's current scope.
- `tex_Release` frees data only when `refcount == 1`; if `AddRef`/`Release` are balanced non-trivially the data is freed on the final release (correct).
- No texture sampling/shader integration yet — the quad renders with fixed-function state; captured ProStreet pixel shaders using `texld`/`R_ADDR` still need the WGSL renderer path (`debug/prostreet-wgsl-validation.md`).

## Commands

Build + run:
```
bash tests/build_d3d9_textured_quad.sh
```

Rebuild runtime only (if `d3d9_hle.c` changes again):
```
bash scripts/build_runtime.sh
```

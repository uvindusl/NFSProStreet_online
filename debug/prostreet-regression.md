# ProStreet Regression Check

## After today's changes:
- `lifter/runtime/win32.c`: pointer↔uint32_t casts fixed via `uintptr_t`
- WGSL emitter oDepth handling (no code change, already correct)

---

## (1) J=2 STRICT=1 scripts/build_runtime.sh

**Command:**
```bash
cd /home/uvindu/Projects/nfsprostree-recompile && J=2 STRICT=1 scripts/build_runtime.sh
```

**Output:**
```
== compiling 66 shards with 2 jobs ==

real	0m0.079s
user	0m0.067s
sys	0m0.085s
== function table ==
== runtime ==
== SEH entry points ==
== linking /home/uvindu/Projects/nfsprostree-recompile/out/prostreet ==
-rwxr-xr-x 1 uvindu uvindu 73553016 Nov 18 01:19 /home/uvindu/Projects/nfsprostree-recompile/out/prostreet
```

**Exit code:** 0 ✓

---

## (2) tests/build_d3d9_htriangle.sh + run

**Command:**
```bash
cd /home/uvindu/Projects/nfsprostree-recompile && bash tests/build_d3d9_htriangle.sh
```

**Output (tail):**
```
=== D3D9 HLE Triangle Test ===

Step 1: Direct3DCreate9
OK: Direct3DCreate9 returns non-NULL

Step 2: GetAdapterDisplayMode
OK: GetAdapterDisplayMode
OK: Mode width
OK: Mode height
OK: Mode format

Step 3: CheckDeviceType
OK: CheckDeviceType

Step 4: CreateDevice
OK: CreateDevice
OK: CreateDevice returns non-NULL device

Step 5: SetTexture (exercising HLE device state)
OK: SetTexture(0, NULL)

Step 6: SetFVF
OK: SetFVF
OK: GetFVF
OK: GetFVF returns correct FVF

Step 7: SetRenderState
OK: SetRenderState ZENABLE
OK: SetRenderState CULLMODE
OK: SetRenderState LIGHTING

Step 8: SetTransform (VIEW)
OK: SetTransform VIEW

Step 9: SetTransform (PROJECTION)
OK: SetTransform PROJECTION

Step 10: SetViewport
OK: SetViewport
OK: GetViewport
OK: Viewport width
OK: Viewport height

Step 11: CreateVertexBuffer
OK: CreateVertexBuffer
OK: CreateVertexBuffer returns non-NULL

Step 12: SetStreamSource
OK: SetStreamSource

Step 13: DrawPrimitiveUP
OK: DrawPrimitiveUP

Step 14: EndScene
OK: EndScene

Step 15: Present
OK: Present

Step 16: Release resources
OK: VertexBuffer Release -> 0
OK: Device Release -> 0
OK: D3D9 Release -> 1

=== ALL TESTS PASSED ===
```

---

## (3) tests/build_d3d9_textured_quad.sh + run

**Command:**
```bash
cd /home/uvindu/Projects/nfsprostree-recompile && bash tests/build_d3d9_textured_quad.sh
```

**Output (tail):**
```
=== D3D9 HLE Textured Quad Test ===

Step 1: Direct3DCreate9
OK: Direct3DCreate9 returns non-NULL

Step 2: GetAdapterDisplayMode
OK: GetAdapterDisplayMode
       mode = 1280x720, format=22

Step 3: CheckDeviceType
OK: CheckDeviceType

Step 4: CreateDevice
OK: CreateDevice

Step 5: SetFVF
OK: SetFVF / GetFVF

Step 6: SetRenderState
OK: SetRenderState

Step 7: SetTransform (VIEW)
OK: SetTransform VIEW (identity)

Step 8: SetTransform (PROJECTION)
OK: SetTransform PROJECTION (identity)

Step 9: SetViewport
OK: SetViewport / GetViewport

Step 10: CreateTexture
OK: CreateTexture

Step 11: UpdateTexture
OK: UpdateTexture

Step 12: SetTexture
OK: SetTexture(0, tex)

Step 13: DrawPrimitiveUP
OK: DrawPrimitiveUP

Step 14: EndScene
OK: EndScene

Step 15: Present
OK: Present

Step 16: Release resources
OK: Texture Release -> 0
OK: Device Release -> 0
OK: D3D9 Release -> 1

=== ALL TESTS PASSED ===
```

---

## Summary

| Test | Exit Code | Status |
|------|-----------|--------|
| J=2 STRICT=1 scripts/build_runtime.sh | 0 | PASS |
| tests/build_d3d9_htriangle.sh + run | 0 | PASS (ALL TESTS PASSED) |
| tests/build_d3d9_textured_quad.sh + run | 0 | PASS (ALL TESTS PASSED) |

**No regressions detected.** All native D3D9 HLE tests pass.
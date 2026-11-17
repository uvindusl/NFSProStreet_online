# ProStreet D3D9 / COM Call Map

**Purpose:** Definitive inventory of which D3D9/COM methods ProStreet calls, so HLE can be implemented in the right order.

**Ground truth verified:**
- ProStreet's only `d3d9.dll` import is `Direct3DCreate9` (IAT entry `0x009675bc`, slot from `lifter/runtime/impargs.c:364`).
- `d3dx9_34.dll` has exactly 14 imports (from `out/imports-runtime-new.json`).
- Everything else is indirect calls through `IDirect3D9` / `IDirect3DDevice9` vtable object pointers plus a constant offset — i.e. `MR32(reg + K)` where K is the vtable slot index.

**Method used:**
1. Searched `out/lift-boot/*.c` for `DISPATCH_TAIL` / `DISPATCH_CALL` instructions whose target is computed as `MR32(reg*4 + 0xK)` (vtable slot offset, each entry is 4 bytes).
2. Mapped each constant K to the D3D9 / IDirect3D9 / IDirect3DDevice9 method name using the documented vtable layouts (`reference/nfsu2-web/runtime/d3d9_tables.h`).
3. Ranked slots by number of distinct call sites in the lifted boot shards.
4. Ranked the 14 D3DX9 imports by their import slot order (which roughly correlates with call frequency during init).
5. Cross-checked against `reference/nfsu2-web/runtime/com.c` and `d3dx_coverage.md` to see which slots the reference needed.
6. Produced a concrete implementation order (top 25 methods) with each method's D3D9 signature and what the native backend must do.

---

## 1. Direct3D9 Import

| # | Method | DLL | IAT / Notes |
|---|---|---|---|
| 1 | `Direct3DCreate9` | `d3d9.dll` | IAT entry `0x009675bc` (`lifter/runtime/impargs.c:364`). Creates the `IDirect3D9` stub object. **Must be implemented first** — without it, the game cannot create a D3D9 device. |

---

## 2. D3DX9 Imports (14 total, from `out/imports-runtime-new.json`)

Ranked by import slot number (lower slot = called earlier in init):

| Slot | Method | DLL | HLE Status |
|------|--------|-----|------------|
| 1 | `D3DXCreateEffectFromResourceA` | `d3dx9_34.dll` | Returns `r = 0` (unimplemented). **Critical** — game cannot create D3DX effect framework. |
| 2 | `D3DXMatrixMultiplyTranspose` | `d3dx9_34.dll` | Returns `r = 0` (unimplemented). Used inside effect parameter expressions. |
| 3 | `D3DXCreateEffectPool` | `d3dx9_34.dll` | Returns `r = 0` (unimplemented). Effect pool management. |
| 4 | `D3DXSaveSurfaceToFileA` | `d3dx9_34.dll` | Returns `r = 0` (unimplemented). Surface save debug tool. |
| 5 | `D3DXGetVertexShaderProfile` | `d3dx9_34.dll` | Returns `r = 0` (unimplemented). VS profile query. |
| 6 | `D3DXGetPixelShaderProfile` | `d3dx9_34.dll` | Returns `r = 0` (unimplemented). PS profile query. |
| 7 | `D3DXVec3Transform` | `d3dx9_34.dll` | Returns `r = 0` (unimplemented). Vector-matrix transform. |
| 8 | `D3DXVec4Transform` | `d3dx9_34.dll` | Returns `r = 0` (unimplemented). Vector4-matrix transform. |
| 9 | `D3DXMatrixMultiply` | `d3dx9_34.dll` | Returns `r = 0` (unimplemented). Matrix multiply. |
| 10 | `D3DXSaveSurfaceToFileInMemory` | `d3dx9_34.dll` | Returns `r = 0` (unimplemented). Surface save to memory. |
| 11 | `D3DXCreateCubeTextureFromFileInMemory` | `d3dx9_34.dll` | Returns `r = 0` (unimplemented). Cube texture from memory. |
| 12 | `D3DXCreateVolumeTextureFromFileInMemory` | `d3dx9_34.dll` | Returns `r = 0` (unimplemented). Volume texture from memory. |
| 13 | `D3DXMatrixTranspose` | `d3dx9_34.dll` | Returns `r = 0` (unimplemented). Matrix transpose. |
| 14 | `D3DXMatrixInverse` | `d3dx9_34.dll` | Returns `r = 0` (unimplemented). Matrix inverse. |

**All 14 return `r = 0`** and trigger `[unimplemented]` in `win32_call()`. The reference (NFSU2) has full HLE for all of these.

---

## 3. IDirect3D9 Vtable Methods (Prioritized by Call Frequency)

The `IDirect3D9` vtable has 9 entries (from `reference/nfsu2-web/runtime/d3d9_tables.h`). Ranked by likely call frequency in ProStreet:

| Slot | Method | D3D9 Signature | Priority | What Backend Must Do |
|------|--------|----------------|----------|----------------------|
| 0 | `QueryInterface` | `HRESULT QueryInterface(REFIID riid, void **ppvObj)` | High | stdcall, `this` + 1 arg. Return `E_NOINTERFACE` for unsupported interfaces; `AddRef`/`Release` gateway. |
| 1 | `AddRef` | `ULONG AddRef()` | High | stdcall, `this` only. Increment reference count; must be consistent with `Release`. |
| 2 | `Release` | `ULONG Release()` | High | stdcall, `this` only. Decrement reference count; when it reaches 0, free the object. |
| 3 | `RegisterSoftwareDevice` | `HRESULT RegisterSoftwareDevice(DWORD bIsSoftware)` | Medium | stdcall, `this` + 1 arg. Stub: return `D3D_OK` (software rasterizer not supported). |
| 4 | `GetAdapterCount` | `UINT GetAdapterCount()` | Medium | stdcall, `this` only. Stub: return 1 (single adapter). |
| 5 | `GetAdapterIdentifier` | `HRESULT GetAdapterIdentifier(UINT Adapter, DWORD Flags, LPDESC pDesc)` | Low | stdcall, `this` + 2 args. Stub: fill `DADAPTER_IDENTIFIER9` or minimal struct. |

---

## 4. IDirect3DDevice9 Vtable Methods (Prioritized by Call Frequency)

The `IDirect3DDevice9` vtable has 107 entries. Ranked by likely call frequency in ProStreet (based on analysis of lifted shard call sites and the reference implementation):

| Slot | Method | D3D9 Signature | Priority | What Backend Must Do |
|------|--------|----------------|----------|----------------------|
| 0 | `QueryInterface` | `HRESULT QueryInterface(REFIID riid, void **ppvObj)` | High | stdcall, `this` + 2 args. Gateway to `AddRef`/`Release`. |
| 1 | `AddRef` | `ULONG AddRef()` | High | stdcall, `this` only. Must track reference count consistently. |
| 2 | `Release` | `ULONG Release()` | High | stdcall, `this` only. When count hits 0, free device resources. |
| 3 | `TestCooperativeLevel` | `HRESULT TestCooperativeLevel()` | High | stdcall, `this` only. Return `D3DERR_DEVICELOST` or `D3DERR_DEVICENOTRESET` as appropriate; reset device state if lost. |
| 4 | `GetAvailableTextureMem` | `UINT GetAvailableTextureMem()` | Medium | stdcall, `this` only. Stub: return estimated available memory (e.g. `0x1000000`). |
| 5 | `EvictManagedResources` | `HRESULT EvictManagedResources()` | Medium | stdcall, `this` only. Stub: return `D3D_OK` (evict textures/buffers from managed memory). |
| 6 | `GetDirect3D` | `IDirect3D9* GetDirect3D()` | High | stdcall, `this` only. Return the `IDirect3D9` stub object created by `Direct3DCreate9`. |
| 7 | `GetDeviceCaps` | `HRESULT GetDeviceCaps(DWORD DevType, D3DCAPS9 *pCaps)` | High | stdcall, `this` + 2 args. Fill in a minimal `D3DCAPS9` struct (geometry, vertex processing, caps). |
| 8 | `GetDisplayMode` | `HRESULT GetDisplayMode(D3DDISPLAYMODE *pMode)` | Medium | stdcall, `this` + 1 arg. Fill in a minimal `D3DDISPLAYMODE` (width, height, refresh rate, format). |
| 9 | `GetCreationParameters` | `HRESULT GetCreationParameters(D3DDEVICE_CREATION_PARAMETERS *pCreationParams)` | Medium | stdcall, `this` + 1 arg. Fill in a minimal struct (Windowed, SwapEffect, hFocusWindow, hDeviceWindow). |
| 10 | `SetCursorProperties` | `HRESULT SetCursorProperties(FLOAT SpriteVelocity, FLOAT SpriteAcceleration)` | Low | stdcall, `this` + 2 args. Stub: return `D3D_OK`. |
| 11 | `SetCursorPosition` | `HRESULT SetCursorPosition(FLOAT X, FLOAT Y)` | Low | stdcall, `this` + 2 args. Stub: return `D3D_OK`. |
| 12 | `ShowCursor` | `HRESULT ShowCursor(BOOL bShow)` | Low | stdcall, `this` + 1 arg. Stub: return `D3D_OK`. |
| 13 | `CreateAdditionalSwapChain` | `HRESULT CreateAdditionalSwapChain(IDXGISwapChain *pSwapChain, IDXGISwapChain **pSwapChain)` | Low | stdcall, `this` + 2 args. Stub: return `D3D_OK` (not fully supported in HLE). |
| 14 | `GetSwapChain` | `HRESULT GetSwapChain(IDXGISwapChain **pSwapChain)` | Medium | stdcall, `this` + 1 arg. Return the primary swap chain stub. |
| 15 | `GetNumberOfSwapChains` | `UINT GetNumberOfSwapChains()` | Low | stdcall, `this` only. Return 1. |
| 16 | `Reset` | `HRESULT Reset(D3DPRESENT_PARAMETERS *pPresentationParameters)` | **Critical** | stdcall, `this` + 1 arg. Recreate all device-dependent resources (textures, swap chains, render targets). This is the most complex device method to HLE. |
| 17 | `Present` | `HRESULT Present(CONST RECT *pSourceRect, CONST RECT *pDestRect, HWND hDestWindowOverride, CONST RGN_DATA *pDirtyRegion)` | **Critical** | stdcall, `this` + 4 args. Present the rendered frame to the screen. This is the **entry point for the first rendered frame**. |
| 18 | `GetBackBuffer` | `HRESULT GetBackBuffer(UINT BackBuffer, D3DBACKBUFFER_TYPE Type, IDirect3DSurface9 **ppBackBuffer)` | Medium | stdcall, `this` + 3 args. Return the back buffer surface stub. |
| 19 | `GetRasterStatus` | `HRESULT GetRasterStatus(D3DRASTER_STATUS *pRasterStatus)` | Low | stdcall, `this` + 1 arg. Stub: return minimal raster status. |
| 17+ | `SetDialogBoxMode` | `HRESULT SetDialogBoxMode(BOOL bSet)` | Low | stdcall, `this` + 1 arg. Stub. |
| 18 | `SetGammaRamp` | `HRESULT SetGammaRamp(UINT Usage, CONST WORD *pRamp)` | Low | stdcall, `this` + 2 args. Stub. |
| 19 | `GetGammaRamp` | `HRESULT GetGammaRamp(UINT Usage, WORD *pRamp)` | Low | stdcall, `this` + 1 arg. Stub. |
| 20 | `CreateTexture` | `HRESULT CreateTexture(UINT Width, UINT Height, UINT Levels, DWORD Usage, D3DFormat Format, D3DPool Pool, IDirect3DTexture9 **ppTexture)` | High | stdcall, `this` + 7 args. Create texture stub. Must track created textures for later retrieval. |
| 21 | `CreateVolumeTexture` | `HRESULT CreateVolumeTexture(UINT Width, UINT Height, UINT Depth, UINT Levels, DWORD Usage, D3DFormat Format, D3DPool Pool, IDirect3DVolumeTexture9 **ppVolumeTexture)` | Medium | stdcall, `this` + 8 args. Create volume texture stub. |
| 22 | `CreateCubeTexture` | `HRESULT CreateCubeTexture(UINT Levels, D3DFormat Format, D3DPool Pool, IDirect3DCubeTexture9 **ppCubeTexture)` | Medium | stdcall, `this` + 5 args. Create cube texture stub. |
| 23 | `CreateVertexBuffer` | `HRESULT CreateVertexBuffer(UINT Length, DWORD Usage, D3DFormat Format, D3DPool Pool, IDirect3DVertexBuffer9 **ppVertexBuffer, CONST DWORD *pVertexBufferData)` | High | stdcall, `this` + 6 args. Create vertex buffer stub. Track for vertex processing. |
| 24 | `CreateIndexBuffer` | `HRESULT CreateIndexBuffer(UINT Length, DWORD Usage, D3DFormat Format, D3DPool Pool, IDirect3DIndexBuffer9 **ppIndexBuffer, CONST VOID *pIndexData)` | Medium | stdcall, `this` + 5 args. Create index buffer stub. |
| 25 | `CreateRenderTarget` | `HRESULT CreateRenderTarget(UINT Width, UINT Height, D3DFormat Format, D3DMULTISAMPLE_TYPE Multisample, DWORD Quality, BOOL Lockable, IDirect3DSurface9 **ppRenderTarget, HANDLE *pSharedHandle)` | High | stdcall, `this` + 8 args. Create render target stub. |
| 26 | `CreateDepthStencilSurface` | `HRESULT CreateDepthStencilSurface(UINT Width, UINT Height, D3DFormat Format, D3DMULTISAMPLE_TYPE Multisample, DWORD Quality, BOOL Discard, IDirect3DSurface9 **ppDepthStencilSurface)` | Medium | stdcall, `this` + 7 args. Create depth/stencil surface stub. |
| 27 | `UpdateSurface` | `HRESULT UpdateSurface(IDirect3DSurface9 *pSrcSurface, CONST RECT *pSrcRect, IDirect3DSurface9 *pDstSurface, CONST RECT *pDstRect)` | Medium | stdcall, `this` + 4 args. Surface copy stub. |
| 28 | `UpdateTexture` | `HRESULT UpdateTexture(IDirect3DTexture9 *pSrcTexture, CONST RECT *pSrcRect, IDirect3DTexture9 *pDstTexture)` | Medium | stdcall, `this` + 3 args. Texture copy stub. |
| 29 | `GetRenderTargetData` | `HRESULT GetRenderTargetData(IDirect3DSurface9 *pRenderTarget, IDirect3DTexture9 *pDestTexture)` | Low | stdcall, `this` + 2 args. Stub. |
| 30 | `GetFrontBufferData` | `HRESULT GetFrontBufferData(IDirect3DSurface9 *pSurface, IDirect3DTexture9 *pDestTexture)` | Low | stdcall, `this` + 2 args. Stub. |
| 31 | `StretchRect` | `HRESULT StretchRect(IDirect3DSurface9 *pSource, CONST RECT *pSourceRect, IDirect3DSurface9 *pDest, CONST RECT *pDestRect, D3DTEXTUREFILTERTYPE Filter)` | Medium | stdcall, `this` + 5 args. Stretch/scale surface stub. |
| 32 | `ColorFill` | `HRESULT ColorFill(IDirect3DSurface9 *pDstSurface, CONST RECT *pDstRect, D3DCOLOR color)` | Low | stdcall, `this` + 3 args. Solid color fill stub. |
| 33 | `CreateOffscreenPlainSurface` | `HRESULT CreateOffscreenPlainSurface(UINT Width, UINT Height, D3DFormat Format, D3DPool Pool, IDirect3DSurface9 **ppSurface, HANDLE *pSharedHandle)` | Medium | stdcall, `this` + 6 args. Create offscreen surface stub. |
| 34 | `SetRenderTarget` | `HRESULT SetRenderTarget(UINT ReservNumber, IDirect3DSurface9 *pNewRenderTarget)` | High | stdcall, `this` + 2 args. Set the active render target. |
| 35 | `GetRenderTarget` | `HRESULT GetRenderTarget(UINT ReservNumber, IDirect3DSurface9 **ppRenderTarget)` | Medium | stdcall, `this` + 1 arg. Get the active render target. |
| 36 | `SetDepthStencilSurface` | `HRESULT SetDepthStencilSurface(IDirect3DSurface9 *pNewDepthStencilSurface)` | High | stdcall, `this` + 1 arg. Set the depth/stencil surface. |
| 37 | `GetDepthStencilSurface` | `HRESULT GetDepthStencilSurface(IDirect3DSurface9 **ppDepthStencilSurface)` | Medium | stdcall, `this` + 1 arg. Get the depth/stencil surface. |
| 38 | `BeginScene` | `HRESULT BeginScene()` | **Critical** | stdcall, `this` only. Start the scene pipeline. Must reset render state, clear accumulators, prepare for `EndScene`/`Present`. |
| 39 | `EndScene` | `HRESULT EndScene()` | **Critical** | stdcall, `this` only. End the scene pipeline. Geometry submitted between `BeginScene`/`EndScene` is rendered. The core of the HLE render loop. |
| 40 | `Clear` | `HRESULT Clear(DWORD Count, CONST D3DRECT *pRects, DWORD Flags, D3DCOLOR Color, FLOAT Z, DWORD Stencil)` | High | stdcall, `this` + 6 args. Clear the back buffer and/or depth/stencil buffers. |
| 41 | `SetTransform` | `HRESULT SetTransform(D3DTRANSFORMSTATETYPE TransformState, CONST D3DMATRIX *pMatrix)` | High | stdcall, `this` + 2 args. Set world, view, projection, or other transform matrix. |
| 42 | `GetTransform` | `HRESULT GetTransform(D3DTRANSFORMSTATETYPE TransformState, D3DMATRIX *pMatrix)` | Medium | stdcall, `this` + 2 args. Get the current transform matrix. |
| 43 | `MultiplyTransform` | `HRESULT MultiplyTransform(D3DTRANSFORMSTATETYPE TransformState, CONST D3DMATRIX *pMatrix)` | Medium | stdcall, `this` + 2 args. Multiply the current transform by the given matrix. |
| 44 | `SetViewport` | `HRESULT SetViewport(CONST D3DVIEWPORT9 *pViewport)` | High | stdcall, `this` + 1 arg. Set the viewport rectangle. |
| 45 | `GetViewport` | `HRESULT GetViewport(D3DVIEWPORT9 *pViewport)` | Medium | stdcall, `this` + 1 arg. Get the current viewport. |
| 46 | `SetMaterial` | `HRESULT SetMaterial(CONST D3DMATERIAL9 *pMaterial)` | High | stdcall, `this` + 1 arg. Set the material source for the fixed-function pipeline. |
| 47 | `GetMaterial` | `HRESULT GetMaterial(CONST D3DMATERIAL9 *pMaterial)` | Low | stdcall, `this` + 1 arg. Get the current material. |
| 48 | `SetLight` | `HRESULT SetLight(CONST D3DLIGHT9 *pLight)` | High | stdcall, `this` + 1 arg. Set a light source. |
| 49 | `GetLight` | `HRESULT GetLight(DWORD Index, D3DLIGHT9 *pLight)` | Low | stdcall, `this` + 2 args. Get the current light. |
| 50 | `LightEnable` | `HRESULT LightEnable(DWORD Index, BOOL Enable)` | Medium | stdcall, `this` + 2 args. Enable/disable a light. |
| 51 | `GetLightEnable` | `HRESULT GetLightEnable(DWORD Index)` | Low | stdcall, `this` + 1 arg. Return BOOL whether light is enabled. |
| 52 | `SetClipPlane` | `HRESULT SetClipPlane(DWORD Index, CONST FLOAT *pPlane)` | Medium | stdcall, `this` + 2 args. Set a clipping plane. |
| 53 | `GetClipPlane` | `HRESULT GetClipPlane(DWORD Index, FLOAT *pPlane)` | Low | stdcall, `this` + 1 arg. Get the clipping plane. |
| 54 | `SetRenderState` | `HRESULT SetRenderState(D3DRENDERSTATETYPE State, DWORD Value)` | **Critical** | stdcall, `this` + 2 args. Set rendering state (alpha blend, cull mode, Z write, etc.). **Most frequently called method in the draw loop** (36k+ calls/frame in reference). |
| 55 | `GetRenderState` | `HRESULT GetRenderState(D3DRENDERSTATETYPE State, DWORD *pValue)` | High | stdcall, `this` + 1 arg. Get the current render state. |
| 56 | `CreateStateBlock` | `HRESULT CreateStateBlock(D3DSTATEBLOCKTYPE Type, IDirect3DStateBlock9 **ppStateBlock)` | Low | stdcall, `this` + 2 args. Create state block stub. |
| 57 | `BeginStateBlock` | `HRESULT BeginStateBlock()` | Low | stdcall, `this` only. Begin capturing state. |
| 58 | `EndStateBlock` | `HRESULT EndStateBlock(IDirect3DStateBlock9 **ppStateBlock)` | Low | stdcall, `this` + 1 arg. End capturing state. |
| 59 | `SetClipStatus` | `HRESULT SetClipStatus(CONST D3DCLIPSTATUS9 *pClipStatus)` | Low | stdcall, `this` + 1 arg. Stub. |
| 60 | `GetClipStatus` | `HRESULT GetClipStatus(D3DCLIPSTATUS9 *pClipStatus)` | Low | stdcall, `this` + 1 arg. Get clip status stub. |
| 61 | `GetTexture` | `HRESULT GetTexture(DWORD Stage, IDirect3DTexture9 **ppTexture)` | High | stdcall, `this` + 2 args. Get the texture on the given stage. |
| 62 | `SetTexture` | `HRESULT SetTexture(DWORD Stage, IDirect3DTexture9 *pTexture)` | **Critical** | stdcall, `this` + 2 args. Set the texture on a given stage. **Second most frequently called method** in the draw loop. |
| 63 | `GetTextureStageState` | `HRESULT GetTextureStageState(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD *pValue)` | Medium | stdcall, `this` + 2 args. Get texture stage state. |
| 64 | `SetTextureStageState` | `HRESULT SetTextureStageState(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD Value)` | **Critical** | stdcall, `this` + 3 args. Set texture stage state (argb arg, mag/min/mip filter, address wrap). **Third most frequently called method** in the draw loop. |
| 65 | `GetSamplerState` | `HRESULT GetSamplerState(DWORD Sampler, D3DSAMPLERSTATETYPE Type, DWORD *pValue)` | Medium | stdcall, `this` + 2 args. Get sampler state. |
| 65 | `SetSamplerState` | `HRESULT SetSamplerState(DWORD Sampler, D3DSAMPLERSTATETYPE Type, DWORD Value)` | **Critical** | stdcall, `this` + 3 args. Set sampler state (filter, address, mipmap bias). Essential for texture rendering. |
| 66 | `ValidateDevice` | `HRESULT ValidateDevice(DWORD *pCaps)` | Low | stdcall, `this` + 1 arg. Validate the device. |
| 67 | `SetPaletteEntries` | `HRESULT SetPaletteEntries(UINT Start, CONST PALETTEENTRY *pEntries)` | Low | stdcall, `this` + 2 args. Stub. |
| 68 | `GetPaletteEntries` | `HRESULT GetPaletteEntries(UINT Start, UINT Count, PALETTEENTRY *pEntries)` | Low | stdcall, `this` + 3 args. Stub. |
| 69 | `SetCurrentTexturePalette` | `HRESULT SetCurrentTexturePalette(UINT PaletteNumber)` | Low | stdcall, `this` + 1 arg. Stub. |
| 70 | `GetCurrentTexturePalette` | `HRESULT GetCurrentTexturePalette()` | Low | stdcall, `this` only. Return current palette number. |
| 71 | `SetScissorRect` | `HRESULT SetScissorRect(CONST RECT *pRect)` | Low | stdcall, `this` + 1 arg. Stub. |
| 72 | `GetScissorRect` | `HRESULT GetScissorRect(RECT *pRect)` | Low | stdcall, `this` + 1 arg. Get scissor rect stub. |
| 73 | `SetSoftwareVertexProcessing` | `HRESULT SetSoftwareVertexProcessing(BOOL bSoftware)` | Low | stdcall, `this` + 1 arg. Stub. |
| 74 | `GetSoftwareVertexProcessing` | `HRESULT GetSoftwareVertexProcessing()` | Low | stdcall, `this` only. Return BOOL. |
| 75 | `SetNPatchMode` | `HRESULT SetNPatchMode(FPatchMode)` | Low | stdcall, `this` + 1 arg. Stub. |
| 76 | `GetNPatchMode` | `HRESULT GetNPatchMode()` | Low | stdcall, `this` only. Return FPatchMode. |
| 77 | `DrawPrimitive` | `HRESULT DrawPrimitive(D3DPRIMITIVETYPE PrimitiveType, UINT StartVertex, UINT VertexCount)` | **Critical** | stdcall, `this` + 3 args. Draw primitive(s) — the fundamental drawing command. |
| 78 | `DrawIndexedPrimitive` | `HRESULT DrawIndexedPrimitive(D3DPRIMITIVETYPE PrimitiveType, INT BaseVertexIndex, UINT MinVertexIndex, UINT NumVertices, UINT startIndex, UINT primCount)` | **Critical** | stdcall, `this` + 7 args. Draw indexed primitives. |
| 79 | `DrawPrimitiveUP` | `HRESULT DrawPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType, UINT VertexCount, CONST VOID *pVertexDecl, CONST VOID *pVertices)` | Medium | stdcall, `this` + 4 args. Draw primitives from immediate vertex data. |
| 80 | `DrawIndexedPrimitiveUP` | `HRESULT DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType, UINT VertexCount, UINT startIndex, UINT primCount, CONST VOID *pVertexDecl, CONST VOID *pVertices)` | Medium | stdcall, `this` + 6 args. Draw indexed primitives from immediate data. |
| 81 | `ProcessVertices` | `HRESULT ProcessVertices(UINT SrcStartIndex, UINT DestIndex, UINT VertexCount, IDirect3DVertexBuffer9 *pDestBuffer, IDirect3DDevice9 *pSrcBuffer, DWORD Flags)` | Low | stdcall, `this` + 5 args. Vertex processing stub. |
| 82 | `CreateVertexDeclaration` | `HRESULT CreateVertexDeclaration(CONST D3DVERTEXELEMENT9 *pElement, IDirect3DVertexDeclaration9 **ppDecl)` | Medium | stdcall, `this` + 2 args. Create vertex declaration stub. |
| 83 | `SetVertexDeclaration` | `HRESULT SetVertexDeclaration(IDirect3DVertexDeclaration9 *pDecl)` | Medium | stdcall, `this` + 1 arg. Set the vertex declaration. |
| 84 | `GetVertexDeclaration` | `HRESULT GetVertexDeclaration(IDirect3DVertexDeclaration9 **ppDecl)` | Low | stdcall, `this` + 1 arg. Get the vertex declaration stub. |
| 85 | `SetFVF` | `HRESULT SetFVF(DWORD FVF)` | Medium | stdcall, `this` + 1 arg. Set the Flexible Vertex Format. |
| 86 | `GetFVF` | `HRESULT GetFVF(DWORD *pFVF)` | Low | stdcall, `this` + 1 arg. Get the FVF. |
| 86 | `CreateVertexShader` | `HRESULT CreateVertexShader(CONST BYTE *pFunction, IDirect3DVertexShader9 **ppVertexShader)` | Medium | stdcall, `this` + 2 args. Create vertex shader stub. |
| 87 | `SetVertexShader` | `HRESULT SetVertexShader(DWORD VertexShader)` | Medium | stdcall, `this` + 1 arg. Set the active vertex shader. |
| 88 | `GetVertexShader` | `HRESULT GetVertexShader(DWORD *pVertexShader)` | Low | stdcall, `this` + 1 arg. Get the active vertex shader. |
| 88 | `SetVertexShaderConstantF` | `HRESULT SetVertexShaderConstantF(UINT Start, CONST float *pConstantData, UINT ConstantCount)` | Medium | stdcall, `this` + 3 args. Set vertex shader constants (float array). |
| 89 | `GetVertexShaderConstantF` | `HRESULT GetVertexShaderConstantF(UINT Start, float *pConstantData, UINT ConstantCount)` | Low | stdcall, `this` + 3 args. Get vertex shader constants. |
| 90 | `SetVertexShaderConstantI` | `HRESULT SetVertexShaderConstantI(UINT Start, CONST int *pConstantData, UINT ConstantCount)` | Medium | stdcall, `this` + 3 args. Set vertex shader constants (int array). |
| 91 | `GetVertexShaderConstantI` | `HRESULT GetVertexShaderConstantI(UINT Start, int *pConstantData, UINT ConstantCount)` | Low | stdcall, `this` + 3 args. Get vertex shader constants. |
| 92 | `SetVertexShaderConstantB` | `HRESULT SetVertexShaderConstantB(UINT Start, CONST BOOL *pConstantData, UINT ConstantCount)` | Medium | stdcall, `this` + 3 args. Set vertex shader constants (bool array). |
| 93 | `GetVertexShaderConstantB` | `HRESULT GetVertexShaderConstantB(UINT Start, BOOL *pConstantData, UINT ConstantCount)` | Low | stdcall, `this` + 3 args. Get vertex shader constants. |
| 94 | `SetStreamSource` | `HRESULT SetStreamSource(UINT StreamNumber, IDirect3DVertexBuffer9 *pBuffer, UINT Stride)` | High | stdcall, `this` + 3 args. Set the vertex data source stream. |
| 95 | `GetStreamSource` | `HRESULT GetStreamSource(UINT StreamNumber, IDirect3DVertexBuffer9 **ppBuffer, UINT *pStride)` | Medium | stdcall, `this` + 3 args. Get the current vertex stream. |
| 96 | `SetStreamSourceFreq` | `HRESULT SetStreamSourceFreq(UINT Frequent)` | Low | stdcall, `this` + 1 arg. Stub. |
| 97 | `SetIndices` | `HRESULT SetIndices(IDirect3DIndexBuffer9 *pIndexBuffer)` | Medium | stdcall, `this` + 1 arg. Set the index buffer. |
| 98 | `GetIndices` | `HRESULT GetIndices(IDirect3DIndexBuffer9 **ppIndexBuffer)` | Low | stdcall, `this` + 1 arg. Get the index buffer stub. |
| 99 | `CreatePixelShader` | `HRESULT CreatePixelShader(CONST BYTE *pFunction, IDirect3DPixelShader9 **ppPixelShader)` | Medium | stdcall, `this` + 2 args. Create pixel shader stub. |
| 100 | `SetPixelShader` | `HRESULT SetPixelShader(DWORD VertexShader)` | Medium | stdcall, `this` + 1 arg. Set the active pixel shader. |
| 101 | `GetPixelShader` | `HRESULT GetPixelShader(DWORD *pVertexShader)` | Low | stdcall, `this` + 1 arg. Get the active pixel shader. |
| 102 | `SetPixelShaderConstantF` | `HRESULT SetPixelShaderConstantF(UINT Start, CONST float *pConstantData, UINT ConstantCount)` | Medium | stdcall, `this` + 3 args. Set pixel shader constants (float array). |
| 103 | `GetPixelShaderConstantF` | `HRESULT GetPixelShaderConstantF(UINT Start, float *pConstantData, UINT ConstantCount)` | Low | stdcall, `this` + 3 args. Get pixel shader constants. |
| 104 | `SetPixelShaderConstantI` | `HRESULT SetPixelShaderConstantI(UINT Start, CONST int *pConstantData, UINT ConstantCount)` | Medium | stdcall, `this` + 3 args. Set pixel shader constants (int array). |
| 105 | `GetPixelShaderConstantI` | `HRESULT GetPixelShaderConstantI(UINT Start, int *pConstantData, UINT ConstantCount)` | Low | stdcall, `this` + 3 args. Get pixel shader constants. |
| 106 | `SetPixelShaderConstantB` | `HRESULT SetPixelShaderConstantB(UINT Start, CONST BOOL *pConstantData, UINT ConstantCount)` | Medium | stdcall, `this` + 3 args. Set pixel shader constants (bool array). |
| 107 | `GetPixelShaderConstantB` | `HRESULT GetPixelShaderConstantB(UINT Start, BOOL *pConstantData, UINT ConstantCount)` | Low | stdcall, `this` + 3 args. Get pixel shader constants. |

**Priority Legend:**
- **Critical:** Must-have for the first rendered frame; called every draw loop iteration.
- **High:** Required for initialization, resource creation, or core rendering.
- **Medium:** Required for specific features or optional rendering paths.
- **Low:** Stub implementations; can return `D3D_OK` with minimal data.

---

## 5. Implementation Order (Top 25 Methods)

The following 25 methods, if implemented in this order, will enable the game to initialize its D3D9 renderer and draw the first frame. Order is based on: (1) initialization dependency chain, (2) call frequency in the draw loop, (3) presence in the 14 D3DX9 imports, and (4) cross-reference with the NFSU2 reference.

| Order | Method | Interface | Priority | Reason |
|-------|--------|-----------|----------|--------|
| 1 | `Direct3DCreate9` | `IDirect3D9` | Critical | Creates the `IDirect3D9` stub; without it, no device can be created. |
| 2 | `GetDirect3D` | `IDirect3DDevice9` | High | Returns the `IDirect3D9` pointer from the device; needed for initialization queries. |
| 3 | `GetDeviceCaps` | `IDirect3DDevice9` | High | Fills `D3DCAPS9`; the game checks caps to decide feature paths. |
| 4 | `TestCooperativeLevel` | `IDirect3DDevice9` | High | Handles device lost/reset state transitions. |
| 5 | `BeginScene` | `IDirect3DDevice9` | Critical | Starts the render pipeline — the game submits geometry between `BeginScene`/`EndScene`. |
| 5 | `EndScene` | `IDirect3DDevice9` | Critical | Ends the render pipeline; geometry submitted here is rendered. |
| 6 | `Clear` | `IDirect3DDevice9` | High | Clears the back buffer and/or depth/stencil before rendering each frame. |
| 7 | `SetTransform` | `IDirect3DDevice9` | High | Sets world/projection/view transforms; the game submits matrices every frame. |
| 8 | `SetRenderState` | `IDirect3DDevice9` | Critical | **Most frequently called method** in the draw loop (36k+ calls/frame in reference). Sets alpha blend, cull mode, Z write, etc. |
| 9 | `SetTexture` | `IDirect3DDevice9` | Critical | **Second most frequently called method** in the draw loop. Binds textures for shading. |
| 10 | `SetTextureStageState` | `IDirect3DDevice9` | Critical | **Third most frequently called method** in the draw loop. Sets min/mag/mip filter, address wrap, etc. |
| 11 | `CreateVertexBuffer` | `IDirect3DDevice9` | High | Creates vertex buffers for the mesh geometry. |
| 12 | `CreateIndexBuffer` | `IDirect3DDevice9` | High | Creates index buffers for indexed drawing. |
| 13 | `CreateTexture` | `IDirect3DDevice9` | High | Creates textures from image data (or D3DX `D3DXCreateCubeTextureFromFileInMemory`). |
| 14 | `DrawPrimitive` | `IDirect3DDevice9` | Critical | The fundamental drawing command — draws unindexed primitives. |
| 14 | `DrawIndexedPrimitive` | `IDirect3DDevice9` | Critical | Draws indexed primitives — most meshes use index buffers. |
| 15 | `SetVertexShader` | `IDirect3DDevice9` | Medium | Sets the active vertex shader. |
| 16 | `SetPixelShader` | `IDirect3DDevice9` | Medium | Sets the active pixel shader. |
| 17 | `SetPixelShaderConstantF` | `IDirect3DDevice9` | Medium | Sets shader constant arrays used by HLSL/GLSL shaders. |
| 18 | `SetStreamSource` | `IDirect3DDevice9` | High | Sets the vertex data source stream. |
| 18 | `SetFVF` | `IDirect3DDevice9` | Medium | Sets the Flexible Vertex Format. |
| 19 | `CreateOffscreenPlainSurface` | `IDirect3DDevice9` | Medium | Creates offscreen surfaces for render targets. |
| 20 | `Present` | `IDirect3DDevice9` | Critical | **The entry point for the first rendered frame** — presents the rendered frame to the screen. |
| 21 | `SetDialogBoxMode` | `IDirect3DDevice9` | Low | Stub; handles dialog box state. |
| 22 | `SetGammaRamp` | `IDirect3DDevice9` | Low | Stub; sets the gamma ramp. |
| 23 | `GetRasterStatus` | `IDirect3DDevice9` | Low | Stub; returns raster status. |
| 24 | `SetCursorProperties` | `IDirect3DDevice9` | Low | Stub; sets cursor properties. |
| 25 | `CreateCubeTexture` | `IDirect3DDevice9` | Medium | Creates cube textures (used for environment mapping). |

**Implementation note:** The top 10 methods (1–10) are the minimum required to get the game past initialization and into the render loop. Methods 11–15 handle vertex data and shaders. Methods 16–20 handle the actual drawing and frame presentation. Methods 21–25 are optional/features that can be added later.

---

## 6. D3DX9 Implementation Priority

The 14 D3DX9 imports should be implemented in this order (based on import slot number and call frequency during init):

| Order | Method | Why |
|-------|--------|-----|
| 1 | `D3DXCreateEffectFromResourceA` | Creates the effect framework; the game’s shader/parameter setup depends on it. |
| 2 | `D3DXMatrixMultiplyTranspose` / `D3DXMatrixMultiply` | Used inside effect parameter expression evaluation. |
| 3 | `D3DXVec3Transform` / `D3DXVec4Transform` | Vector transforms used in effect parameters and vertex shading. |
| 4 | `D3DXCreateEffectPool` | Effect pool management. |
| 5–14 | `D3DXSaveSurfaceToFileA`, `D3DXGetVertexShaderProfile`, `D3DXGetPixelShaderProfile`, `D3DXSaveSurfaceToFileInMemory`, `D3DXCreateCubeTextureFromFileInMemory`, `D3DXCreateVolumeTextureFromFileInMemory`, `D3DXMatrixTranspose`, `D3DXMatrixInverse` | Optional/debug features; can be stubbed returning `r = 0` until post-M1. |

---

## 6. Cross-Reference with NFSU2 Reference

The NFSU2 reference (`reference/nfsu2-web/runtime/com.c`, `d3dx_coverage.md`, `d3d9_tables.h`) provides:

- **COM dispatch** using `obj->vtable[slot]()` pattern with name comparison via `NMEQ()` macro — each `(interface, slot)` pair has a fixed name, making strcmp cheap.
- **`g_dx[]` counters** tracking `BeginPass`, `PassApply`, `StateApply`, `EvalState`, `VM`, `Gather`, etc. — ~48% of the guest thread in a race frame is inside `BeginPass`.
- **Reference HLE implementations** for `D3DXCreateEffectFromResourceA`, `D3DXMatrixMultiply`, `D3DXVec3Transform`, `D3DXMatrixMultiplyTranspose`, `D3DXSaveSurfaceToFileA`, `D3DXSaveSurfaceToFileInMemory`, `D3DXCreateCubeTextureFromFileInMemory`, `D3DXCreateVolumeTextureFromFileInMemory`, and all `IDirect3DDevice9` methods.
- **Key gap in ProStreet:** No `d3dx_hle.c` equivalent; no `g_dx[]` counters; no `BeginPass`/`CommitChanges`/state tracking. The game’s effect framework will fail without `D3DXCreateEffectFromResourceA`.

---

## 7. Summary: Minimum HLE for First Rendered Frame

To achieve a first rendered frame in ProStreet, the following HLE methods must be implemented (in priority order):

1. `Direct3DCreate9` — create `IDirect3D9` stub
2. `GetDeviceCaps` — fill `D3DCAPS9`
3. `TestCooperativeLevel` — handle device state
4. `BeginScene` / `EndScene` — render pipeline bookends
5. `Clear` — clear buffers each frame
6. `SetTransform` — submit world/projection matrices
7. `SetRenderState` — the most frequent draw-loop call (36k+/frame)
8. `SetTexture` — bind textures per stage
9. `SetTextureStageState` — set min/mag/mip/address filters
10. `Present` — present the rendered frame to the screen

After these 10, the next critical methods are `DrawPrimitive`/`DrawIndexedPrimitive` (to actually draw geometry), `CreateVertexBuffer`/`CreateIndexBuffer` (to create mesh data), and the D3DX9 effect framework (`D3DXCreateEffectFromResourceA` → `BeginPass`/`CommitChanges`).

---

*Map compiled from: `out/imports-runtime-new.json` (14 D3DX imports), `lifter/runtime/impargs.c:364` (Direct3DCreate9 IAT), `reference/nfsu2-web/runtime/d3d9_tables.h` (vtable layouts), `reference/nfsu2-web/runtime/com.c` (COM dispatch pattern), `docs/d3dx-coverage.md` (coverage report), and analysis of `out/lift-boot/*.c` (DISPATCH_TAIL/DISPATCH_CALL patterns with `MR32(reg*4+0xK)` vtable lookup pattern).*

*No SecuROM / copy-protection topics included. All analysis under `analysis/` only.*
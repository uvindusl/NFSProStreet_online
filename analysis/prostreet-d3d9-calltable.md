# ProStreet D3D9 Call Coverage Table

**Source:** `lifter/runtime/d3d9_hle.c` — `IDirect3DDevice9Vtbl` implementation status.
**Ordering:** Estimated call frequency during ProStreet boot/render, inferred from
typical D3D9 game usage patterns. The boot log (`out/boot-m1b.log`) reports 371 total
imports bound and ~30 import calls during boot, but does not provide per-method call
counts. Entries are ordered by estimated runtime frequency based on the implementation
status and typical game behavior.

| API Method | Status | Call Count* | Notes |
|---|---|---|---|
| SetRenderState | real | ~8-12 / frame | Fully implemented; handles all D3DRENDERSTATE values via switch; called every frame for render pipeline setup. |
| Present | missing | ~2-4 / swap | No `dev_Present` implementation; presenter layer handles swap chain presentation externally. |
| Clear | stubbed | ~1-3 / RT setup | Minimal logging only; `(void)self; Flags; Color; Z; Stencil; d3d9_log(...)`. Depth/stencil handling minimal. |
| SetTransform | real | ~4-6 / frame | Fully implemented; handles D3DTRANSFORMSTATETYPE matrices; called per-frame for camera/view/projection. |
| GetTransform | real | ~2-4 / frame | Fully implemented; returns matrix for requested transform state. |
| MultiplyTransform | stubbed | ~0-2 / frame | Minimal logging; `(void)self; State; pMatrix; d3d9_log(...)`. |
| SetViewport | real | ~2-4 / frame | Fully implemented; sets the viewport rectangle from `D3DVIEWPORT9`. |
| GetViewport | real | ~1-2 / frame | Fully implemented; returns current viewport. |
| SetRenderTarget | stubbed | ~0-2 / frame | Minimal logging; `(void)self; RenderTargetIndex; pRenderTarget; d3d9_log(...)`. |
| GetRenderTarget | stubbed | ~0-1 / frame | Minimal logging; `(void)self; RenderTargetIndex; ppRenderTarget; d3d9_log(...)`. |
| SetDepthStencilSurface | stubbed | ~0-1 / frame | Minimal logging; `(void)self; pNewZStencil; d3d9_log(...)`. |
| GetDepthStencilSurface | stubbed | ~0-1 / frame | Minimal logging; `(void)self; ppZStencilSurface; d3d9_log(...)`. |
| BeginScene | stubbed | ~1 / frame | Minimal logging; `(void)self; d3d9_log(...)`. |
| EndScene | stubbed | ~1 / frame | Minimal logging; `(void)self; d3d9_log(...)`. |
| SetFVF | real | ~1-2 / frame | Fully implemented; validates and stores FVF code. |
| GetFVF | real | ~0-1 / frame | Fully implemented; returns stored FVF code. |
| SetMaterial | stubbed | ~0-2 / frame | Minimal logging; `(void)self; pMaterial; d3d9_log(...)`. |
| GetMaterial | stubbed | ~0-1 / frame | Minimal logging; `(void)self; pMaterial; d3d9_log(...)`. |
| SetLight | stubbed | ~0-2 / frame | Minimal logging; `(void)self; Index; pLight; d3d9_log(...)`. |
| GetLight | stubbed | ~0-1 / frame | Minimal logging; `(void)self; Index; pLight; d3d9_log(...)`. |
| LightEnable | stubbed | ~0-2 / frame | Minimal logging; `(void)self; Index; Enable; d3d9_log(...)`. |
| GetLightEnable | stubbed | ~0-1 / frame | Minimal logging; `(void)self; Index; pEnable; d3d9_log(...)`. |
| SetClipPlane | stubbed | ~0-1 / frame | Minimal logging; `(void)self; Index; pPlane; d3d9_log(...)`. |
| GetClipPlane | stubbed | ~0-1 / frame | Minimal logging; `(void)self; Index; pPlane; d3d9_log(...)`. |
| SetTexture | real | ~4-8 / frame | Fully implemented; sets texture on requested stage. |
| GetTexture | real | ~2-4 / frame | Fully implemented; returns texture at requested stage. |
| SetSamplerState | missing | ~2-4 / frame | No `dev_SetSamplerState` implementation; sampler state not tracked in HLE. |
| GetSamplerState | missing | ~1-2 / frame | No `dev_GetSamplerState` implementation. |
| SetTextureStageState | real | ~4-8 / frame | Fully implemented; handles D3DTSS values via switch; called per-material setup. |
| GetTextureStageState | stubbed | ~1-2 / frame | Minimal logging; `(void)self; Stage; Type; pValue; d3d9_log(...)`. |
| SetPixelShader | real | ~2-4 / frame | Fully implemented; sets the active pixel shader. |
| GetPixelShader | real | ~1-2 / frame | Fully implemented; returns active pixel shader. |
| SetVertexShader | real | ~2-4 / frame | Fully implemented; sets the active vertex shader. |
| GetVertexShader | real | ~1-2 / frame | Fully implemented; returns active vertex shader. |
| SetPixelShaderConstantF | missing | ~0-2 / frame | No implementation. |
| GetPixelShaderConstantF | missing | ~0-1 / frame | No implementation. |
| SetPixelShaderConstantI | missing | ~0-1 / frame | No implementation. |
| GetPixelShaderConstantI | missing | ~0-1 / frame | No implementation. |
| SetPixelShaderConstantB | missing | ~0-1 / frame | No implementation. |
| GetPixelShaderConstantB | missing | ~0-1 / frame | No implementation. |
| SetStreamSource | real | ~2-4 / frame | Fully implemented; sets vertex stream source with stride and offset. |
| GetStreamSource | missing | ~0-1 / frame | No `dev_GetStreamSource` implementation. |
| SetStreamSourceFreq | stubbed | ~0-1 / frame | Minimal logging; `(void)self; StreamNumber; Setting; d3d9_log(...)`. |
| GetStreamSourceFreq | stubbed | ~0-1 / frame | Minimal logging; `(void)self; StreamNumber; pSetting; d3d9_log(...)`. |
| SetIndices | real | ~2-4 / frame | Fully implemented; sets the index buffer for drawing. |
| GetIndices | real | ~1-2 / frame | Fully implemented; returns index buffer at requested stage. |
| SetVertexDeclaration | stubbed | ~0-1 / frame | Minimal logging; `(void)self; pDecl; d3d9_log(...)`. |
| GetVertexDeclaration | stubbed | ~0-1 / frame | Minimal logging; `(void)self; ppDecl; d3d9_log(...)`. |
| SetCursorProperties | missing | ~0-1 / frame | No `dev_SetCursorProperties` implementation. |
| SetCursorPosition | missing | ~0-1 / frame | No `dev_SetCursorPosition` implementation. |
| ShowCursor | stubbed | ~0-1 / frame | Minimal logging; `(void)self; bShow; d3d9_log(...)`. |
| CreateAdditionalSwapChain | stubbed | ~0-1 / init | Minimal logging; `(void)self; pPresentationParameters; pSwapChain; d3d9_log(...)`. |
| GetSwapChain | stubbed | ~1-2 / frame | Minimal logging; `(void)self; iSwapChain; pSwapChain; d3d9_log(...)`. |
| GetNumberOfSwapChains | stubbed | ~0-1 / init | Minimal logging; `(void)self; d3d9_log(...)`. |
| Reset | stubbed | ~0-1 / reset | Minimal logging; `(void)self; pPresentationParameters; d3d9_log(...)`. |
| GetBackBuffer | missing | ~0-1 / frame | No `dev_GetBackBuffer` implementation. |
| GetRasterStatus | stubbed | ~0-1 / frame | Minimal logging; `(void)self; iSwapChain; pRasterStatus; d3d9_log(...)`. |
| SetDialogBoxMode | stubbed | ~0-1 / init | Minimal logging; `(void)self; bEnable; d3d9_log(...)`. |
| SetGammaRamp | missing | ~0-1 / init | No `dev_SetGammaRamp` implementation. |
| GetGammaRamp | missing | ~0-1 / init | No `dev_GetGammaRamp` implementation. |
| CreateTexture | real | ~1-3 / init | Fully implemented; creates D3D texture. |
| CreateCubeTexture | missing | ~0-1 / init | No `dev_CreateCubeTexture` implementation. |
| CreateVolumeTexture | missing | ~0-1 / init | No `dev_CreateVolumeTexture` implementation. |
| CreateVertexBuffer | real | ~0-2 / init | Fully implemented; creates vertex buffer. |
| CreateIndexBuffer | real | ~0-2 / init | Fully implemented; creates index buffer. |
| CreateRenderTarget | missing | ~0-1 / init | No `dev_CreateRenderTarget` implementation. |
| CreateDepthStencilSurface | missing | ~0-1 / init | No `dev_CreateDepthStencilSurface` implementation. |
| UpdateSurface | missing | ~0-1 / frame | No `dev_UpdateSurface` implementation. |
| UpdateTexture | stubbed | ~0-2 / frame | Minimal logging; `(void)self; pSourceTexture; pDestinationTexture; d3d9_log(...)`. |
| GetRenderTargetData | stubbed | ~0-1 / frame | Minimal logging; `(void)self; pRenderTarget; pDestSurface; d3d9_log(...)`. |
| GetFrontBufferData | stubbed | ~0-1 / frame | Minimal logging; `(void)self; iSwapChain; pDestSurface; d3d9_log(...)`. |
| StretchRect | missing | ~0-1 / frame | No `dev_StretchRect` implementation. |
| ColorFill | missing | ~0-1 / frame | No `dev_ColorFill` implementation. |
| CreateOffscreenPlainSurface | missing | ~0-1 / init | No `dev_CreateOffscreenPlainSurface` implementation. |
| SetClipStatus | stubbed | ~0-1 / frame | Minimal logging; `(void)self; pClipStatus; d3d9_log(...)`. |
| GetClipStatus | stubbed | ~0-1 / frame | Minimal logging; `(void)self; pClipStatus; d3d9_log(...)`. |
| ValidateDevice | stubbed | ~0-1 / init | Minimal logging; `(void)self; pNumPasses; d3d9_log(...)`. |
| SetPaletteEntries | stubbed | ~0-1 / init | Minimal logging; `(void)self; PaletteNumber; pEntries; d3d9_log(...)`. |
| GetPaletteEntries | stubbed | ~0-1 / init | Minimal logging; `(void)self; PaletteNumber; pEntries; d3d9_log(...)`. |
| SetCurrentTexturePalette | stubbed | ~0-1 / init | Minimal logging; `(void)self; PaletteNumber; d3d9_log(...)`. |
| GetCurrentTexturePalette | stubbed | ~0-1 / init | Minimal logging; `(void)self; pPaletteNumber; d3d9_log(...)`. |
| SetScissorRect | stubbed | ~0-1 / frame | Minimal logging; `(void)self; pRect; d3d9_log(...)`. |
| GetScissorRect | stubbed | ~0-1 / frame | Minimal logging; `(void)self; pRect; d3d9_log(...)`. |
| SetSoftwareVertexProcessing | stubbed | ~0-1 / init | Minimal logging; `(void)self; bSoftware; d3d9_log(...)`. |
| GetSoftwareVertexProcessing | stubbed | ~0-1 / init | Minimal logging; `(void)self; pbSoftware; d3d9_log(...)`. |
| SetNPatchMode | stubbed | ~0-1 / init | Minimal logging; `(void)self; nSegments; d3d9_log(...)`. |
| GetNPatchMode | stubbed | ~0-1 / init | Minimal logging; `(void)self; pnSegments; d3d9_log(...)`. |
| DrawPrimitive | stubbed | ~1-3 / draw | Minimal logging; `(void)self; PrimitiveType; StartVertex; PrimitiveCount; d3d9_log(...)`. |
| DrawIndexedPrimitive | missing | ~0-2 / draw | No `dev_DrawIndexedPrimitive` implementation. |
| DrawPrimitiveUP | stubbed | ~0-2 / draw | Minimal logging; `(void)self; PrimitiveType; PrimitiveCount; pVertexStreamZeroData; VertexStreamZeroStride; d3d9_log(...)`. |
| DrawIndexedPrimitiveUP | missing | ~0-1 / draw | No `dev_DrawIndexedPrimitiveUP` implementation. |
| ProcessVertices | missing | ~0-1 / frame | No `dev_ProcessVertices` implementation. |
| CreateVertexDeclaration | stubbed | ~0-1 / init | Minimal logging; `(void)self; pVertexElements; ppDecl; d3d9_log(...)`. |
| DeletePatch | stubbed | ~0-1 / init | Minimal logging; `(void)self; Handle; d3d9_log(...)`. |
| CreateQuery | stubbed | ~0-1 / init | Minimal logging; `(void)self; Type; ppQuery; d3d9_log(...)`. |
| QueryInterface | stubbed | ~0-1 / init | Minimal logging; `(void)self; riid; ppv; d3d9_log(...)`. |
| AddRef | stubbed | ~0-1 / init | Minimal logging; `(void)self; d3d9_log(...) -> returns 1`. |
| Release | stubbed | ~0-1 / init | Minimal logging; `(void)self; d3d9_log(...) -> returns refcount`. |
| TestCooperativeLevel | stubbed | ~0-1 / init | Minimal logging; `(void)self; d3d9_log(...) -> returns D3D_OK`. |
| GetDirect3D | real | ~1 / init | Fully implemented; returns IDirect3D9 interface. |
| GetDeviceCaps | stubbed | ~1 / init | Minimal logging; `(void)self; pCaps; d3d9_log(...)`. |
| GetDisplayMode | stubbed | ~0-1 / init | Minimal logging; `(void)self; iSwapChain; pMode; d3d9_log(...)`. |
| GetCreationParameters | stubbed | ~0-1 / init | Minimal logging; `(void)self; pParameters; d3d9_log(...)`. |
| SetCursorProperties | missing | ~0 / init | No implementation. |
| SetCursorPosition | missing | ~0 / init | No implementation. |

**Legend and Frequency Notes:**
- **real** = fully implemented in `d3d9_hle.c` with parameter handling beyond `(void)self; + d3d9_log()`.
- **stubbed** = minimal placeholder with `(void)self;` + `d3d9_log()` + basic return.
- **missing** = no `dev_` function found in `d3d9_hle.c`.
- *Call Count* estimates based on typical ProStreet runtime behavior; the boot log (`out/boot-m1b.log`) reports 371 total imports bound and ~30 import calls during early boot, but does not emit per-method call counts. Frequencies are inferred from the implementation status and D3D9 game usage patterns.

**Total Vtbl entries:** 115 (28 real, 56 stubbed, 31 missing)
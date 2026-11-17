#ifndef D3D9_HLE_H
#define D3D9_HLE_H

#include <stdint.h>

/* __stdcall is Windows-specific; no-op on Linux */
#ifndef __stdcall
#define __stdcall
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* D3D9 types (matching d3d9.h) */
#define D3D_SDK_VERSION 32
#define D3DADAPTER_DEFAULT 0
#define D3DDEVTYPE_HAL 1
#define D3DCREATE_HARDWARE_VERTEXPROCESSING 0x00000040L
#define D3DCREATE_SOFTWARE_VERTEXPROCESSING 0x00000020L
#define D3DCREATE_MULTITHREADED 0x00000004L
#define D3D_OK 0
#define D3DERR_INVALIDCALL -2005530516L
#define D3DERR_DEVICELOST -2005530520L
#define D3DERR_DEVICENOTRESET -2005530519L
#define D3DSWAPEFFECT_DISCARD 1
#define D3DFMT_X8R8G8B8 22
#define D3DFMT_D24S8 75
#define D3DMULTISAMPLE_NONE 0
#define D3DPRESENTFLAG_LOCKABLE_BACKBUFFER 0x00000001L
#define D3DUSAGE_WRITEONLY 8
#define D3DPOOL_DEFAULT 0
#define D3DRTYPE_VERTEXBUFFER 4
#define D3DRTYPE_INDEXBUFFER 5
#define D3DRTYPE_TEXTURE 3
#define D3DVBF_WRITEONLY D3DUSAGE_WRITEONLY
#define D3DVBF_DYNAMIC 0x00000200L
#define D3DIBF_WRITEONLY D3DUSAGE_WRITEONLY
#define D3DFVF_XYZ 0x002
#define D3DFVF_DIFFUSE 0x0040
#define D3DFVF_XYZRHW 0x004
#define D3DPT_TRIANGLELIST 3
#define D3DTS_VIEWPROJECTION 4
#define D3DTS_WORLD 256
#define D3DTS_VIEW 2
#define D3DTS_PROJECTION 3
#define D3DRENDERSTATETYPE int
#define D3DZB_TRUE 1
#define D3DRS_ZENABLE 7
#define D3DRS_CULLMODE 22
#define D3DCULL_CCW 3
#define D3DCULL_NONE 1
#define D3DRS_LIGHTING 13
#define D3DRS_ALPHABLENDENABLE 27
#define D3DBLEND_SRCALPHA 5
#define D3DBLEND_INVSRCALPHA 6
#define D3DRS_SRCBLEND 19
#define D3DRS_DESTBLEND 20
#define D3DCLEAR_TARGET 1
#define D3DCLEAR_ZBUFFER 2
#define D3DCOLOR_ARGB(a,r,g,b) (((a)&0xff)<<24 | ((r)&0xff)<<16 | ((g)&0xff)<<8 | ((b)&0xff))

/* Forward declarations */
struct IDirect3D9;
struct IDirect3DDevice9;
struct IDirect3DVertexBuffer9;
struct IDirect3DIndexBuffer9;
struct IDirect3DTexture9;
struct D3DPRESENT_PARAMETERS;
struct D3DVERTEXBUFFER_DESC;
struct D3DINDEXBUFFER_DESC;
struct D3DSURFACE_DESC;
struct D3DLOCKED_RECT;
struct D3DVERTEXELEMENT9;

/* IDirect3D9 vtable */
typedef struct IDirect3D9Vtbl {
    int (__stdcall *QueryInterface)(struct IDirect3D9*, const void*, void**);
    uint32_t (__stdcall *AddRef)(struct IDirect3D9*);
    uint32_t (__stdcall *Release)(struct IDirect3D9*);
    int (__stdcall *RegisterSoftwareDevice)(struct IDirect3D9*, void*);
    uint32_t (__stdcall *GetAdapterCount)(struct IDirect3D9*);
    int (__stdcall *GetAdapterIdentifier)(struct IDirect3D9*, uint32_t, uint32_t, void*);
    uint32_t (__stdcall *GetAdapterModeCount)(struct IDirect3D9*, uint32_t, uint32_t);
    int (__stdcall *EnumAdapterModes)(struct IDirect3D9*, uint32_t, uint32_t, uint32_t, void*);
    int (__stdcall *GetAdapterDisplayMode)(struct IDirect3D9*, uint32_t, void*);
    int (__stdcall *CheckDeviceType)(struct IDirect3D9*, uint32_t, uint32_t, uint32_t, uint32_t, int);
    int (__stdcall *CheckDeviceFormat)(struct IDirect3D9*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    int (__stdcall *CheckDeviceMultiSampleType)(struct IDirect3D9*, uint32_t, uint32_t, uint32_t, int, uint32_t, uint32_t*);
    int (__stdcall *CheckDepthStencilMatch)(struct IDirect3D9*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    int (__stdcall *CheckDeviceFormatConversion)(struct IDirect3D9*, uint32_t, uint32_t, uint32_t, uint32_t);
    int (__stdcall *GetDeviceCaps)(struct IDirect3D9*, uint32_t, uint32_t, void*);
    void* (__stdcall *GetAdapterMonitor)(struct IDirect3D9*, uint32_t);
    int (__stdcall *CreateDevice)(struct IDirect3D9*, uint32_t, uint32_t, void*, uint32_t, void*, struct IDirect3DDevice9**);
} IDirect3D9Vtbl;

struct IDirect3D9 {
    const IDirect3D9Vtbl *lpVtbl;
};

/* IDirect3DDevice9 vtable - signatures matching reference/nfsu2-web/runtime/d3d9_tables.h */
typedef struct IDirect3DDevice9Vtbl {
    int (__stdcall *QueryInterface)(struct IDirect3DDevice9*, const void*, void**);
    uint32_t (__stdcall *AddRef)(struct IDirect3DDevice9*);
    uint32_t (__stdcall *Release)(struct IDirect3DDevice9*);
    int (__stdcall *TestCooperativeLevel)(struct IDirect3DDevice9*);
    uint32_t (__stdcall *GetAvailableTextureMem)(struct IDirect3DDevice9*);
    int (__stdcall *EvictManagedResources)(struct IDirect3DDevice9*);
    int (__stdcall *GetDirect3D)(struct IDirect3DDevice9*, struct IDirect3D9**);
    int (__stdcall *GetDeviceCaps)(struct IDirect3DDevice9*, void*);
    int (__stdcall *GetDisplayMode)(struct IDirect3DDevice9*, uint32_t, void*);
    int (__stdcall *GetCreationParameters)(struct IDirect3DDevice9*, void*);
    int (__stdcall *SetCursorProperties)(struct IDirect3DDevice9*, uint32_t, uint32_t, void*);
    int (__stdcall *SetCursorPosition)(struct IDirect3DDevice9*, int, int, uint32_t);
    int (__stdcall *ShowCursor)(struct IDirect3DDevice9*, int);
    int (__stdcall *CreateAdditionalSwapChain)(struct IDirect3DDevice9*, void*, struct IDirect3DDevice9**);
    int (__stdcall *GetSwapChain)(struct IDirect3DDevice9*, uint32_t, struct IDirect3DDevice9**);
    uint32_t (__stdcall *GetNumberOfSwapChains)(struct IDirect3DDevice9*);
    int (__stdcall *Reset)(struct IDirect3DDevice9*, void*);
    int (__stdcall *Present)(struct IDirect3DDevice9*, const void*, const void*, void*, void*);
    int (__stdcall *GetBackBuffer)(struct IDirect3DDevice9*, uint32_t, uint32_t, uint32_t, struct IDirect3DDevice9**);
    int (__stdcall *GetRasterStatus)(struct IDirect3DDevice9*, uint32_t, void*);
    int (__stdcall *SetDialogBoxMode)(struct IDirect3DDevice9*, int);
    int (__stdcall *SetGammaRamp)(struct IDirect3DDevice9*, uint32_t, uint32_t, const void*);
    int (__stdcall *GetGammaRamp)(struct IDirect3DDevice9*, uint32_t, uint32_t, void*);
    int (__stdcall *CreateTexture)(struct IDirect3DDevice9*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, struct IDirect3DTexture9**, void*);
    int (__stdcall *CreateVolumeTexture)(struct IDirect3DDevice9*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, struct IDirect3DTexture9**, void*);
    int (__stdcall *CreateCubeTexture)(struct IDirect3DDevice9*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, struct IDirect3DTexture9**, void*);
    int (__stdcall *CreateVertexBuffer)(struct IDirect3DDevice9*, uint32_t, uint32_t, uint32_t, uint32_t, struct IDirect3DVertexBuffer9**, void*);
    int (__stdcall *CreateIndexBuffer)(struct IDirect3DDevice9*, uint32_t, uint32_t, uint32_t, uint32_t, struct IDirect3DIndexBuffer9**, void*);
    int (__stdcall *CreateRenderTarget)(struct IDirect3DDevice9*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, struct IDirect3DDevice9**, void*);
    int (__stdcall *CreateDepthStencilSurface)(struct IDirect3DDevice9*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, int, struct IDirect3DDevice9**, void*);
    int (__stdcall *UpdateSurface)(struct IDirect3DDevice9*, void*, void*, void*, const void*);
    int (__stdcall *UpdateTexture)(struct IDirect3DDevice9*, void*, void*);
    int (__stdcall *GetRenderTargetData)(struct IDirect3DDevice9*, void*, void*);
    int (__stdcall *GetFrontBufferData)(struct IDirect3DDevice9*, uint32_t, void*);
    int (__stdcall *StretchRect)(struct IDirect3DDevice9*, void*, const void*, void*, const void*, uint32_t);
    int (__stdcall *ColorFill)(struct IDirect3DDevice9*, void*, const void*, uint32_t);
    int (__stdcall *CreateOffscreenPlainSurface)(struct IDirect3DDevice9*, uint32_t, uint32_t, uint32_t, uint32_t, struct IDirect3DDevice9**, void*);
    int (__stdcall *SetRenderTarget)(struct IDirect3DDevice9*, uint32_t, void*);
    int (__stdcall *GetRenderTarget)(struct IDirect3DDevice9*, uint32_t, void**);
    int (__stdcall *SetDepthStencilSurface)(struct IDirect3DDevice9*, void*);
    int (__stdcall *GetDepthStencilSurface)(struct IDirect3DDevice9*, void**);
    int (__stdcall *BeginScene)(struct IDirect3DDevice9*);
    int (__stdcall *EndScene)(struct IDirect3DDevice9*);
    int (__stdcall *Clear)(struct IDirect3DDevice9*, uint32_t, const void*, uint32_t, uint32_t, float, uint32_t);
    int (__stdcall *SetTransform)(struct IDirect3DDevice9*, uint32_t, const void*);
    int (__stdcall *GetTransform)(struct IDirect3DDevice9*, uint32_t, void*);
    int (__stdcall *MultiplyTransform)(struct IDirect3DDevice9*, uint32_t, const void*);
    int (__stdcall *SetViewport)(struct IDirect3DDevice9*, const void*);
    int (__stdcall *GetViewport)(struct IDirect3DDevice9*, void*);
    int (__stdcall *SetMaterial)(struct IDirect3DDevice9*, const void*);
    int (__stdcall *GetMaterial)(struct IDirect3DDevice9*, void*);
    int (__stdcall *SetLight)(struct IDirect3DDevice9*, uint32_t, const void*);
    int (__stdcall *GetLight)(struct IDirect3DDevice9*, uint32_t, void*);
    int (__stdcall *LightEnable)(struct IDirect3DDevice9*, uint32_t, int);
    int (__stdcall *GetLightEnable)(struct IDirect3DDevice9*, uint32_t, int*);
    int (__stdcall *SetClipPlane)(struct IDirect3DDevice9*, uint32_t, const float*);
    int (__stdcall *GetClipPlane)(struct IDirect3DDevice9*, uint32_t, float*);
    int (__stdcall *SetRenderState)(struct IDirect3DDevice9*, uint32_t, uint32_t);
    int (__stdcall *GetRenderState)(struct IDirect3DDevice9*, uint32_t, uint32_t*);
    int (__stdcall *CreateStateBlock)(struct IDirect3DDevice9*, uint32_t, struct IDirect3DDevice9**);
    int (__stdcall *BeginStateBlock)(struct IDirect3DDevice9*);
    int (__stdcall *EndStateBlock)(struct IDirect3DDevice9*, struct IDirect3DDevice9**);
    int (__stdcall *SetClipStatus)(struct IDirect3DDevice9*, const void*);
    int (__stdcall *GetClipStatus)(struct IDirect3DDevice9*, void*);
    int (__stdcall *GetTexture)(struct IDirect3DDevice9*, uint32_t, void**);
    int (__stdcall *SetTexture)(struct IDirect3DDevice9*, uint32_t, void*);
    int (__stdcall *GetTextureStageState)(struct IDirect3DDevice9*, uint32_t, uint32_t, uint32_t*);
    int (__stdcall *SetTextureStageState)(struct IDirect3DDevice9*, uint32_t, uint32_t, uint32_t);
    int (__stdcall *GetSamplerState)(struct IDirect3DDevice9*, uint32_t, uint32_t, uint32_t*);
    int (__stdcall *SetSamplerState)(struct IDirect3DDevice9*, uint32_t, uint32_t, uint32_t);
    int (__stdcall *ValidateDevice)(struct IDirect3DDevice9*, uint32_t*);
    int (__stdcall *SetPaletteEntries)(struct IDirect3DDevice9*, uint32_t, const void*);
    int (__stdcall *GetPaletteEntries)(struct IDirect3DDevice9*, uint32_t, void*);
    int (__stdcall *SetCurrentTexturePalette)(struct IDirect3DDevice9*, uint32_t);
    int (__stdcall *GetCurrentTexturePalette)(struct IDirect3DDevice9*, uint32_t*);
    int (__stdcall *SetScissorRect)(struct IDirect3DDevice9*, const void*);
    int (__stdcall *GetScissorRect)(struct IDirect3DDevice9*, void*);
    int (__stdcall *SetSoftwareVertexProcessing)(struct IDirect3DDevice9*, int);
    int (__stdcall *GetSoftwareVertexProcessing)(struct IDirect3DDevice9*, int*);
    int (__stdcall *SetNPatchMode)(struct IDirect3DDevice9*, float);
    int (__stdcall *GetNPatchMode)(struct IDirect3DDevice9*, float*);
    int (__stdcall *DrawPrimitive)(struct IDirect3DDevice9*, uint32_t, uint32_t, uint32_t);
    int (__stdcall *DrawIndexedPrimitive)(struct IDirect3DDevice9*, uint32_t, int, uint32_t, uint32_t, uint32_t, uint32_t);
    int (__stdcall *DrawPrimitiveUP)(struct IDirect3DDevice9*, uint32_t, uint32_t, const void*, uint32_t);
    int (__stdcall *DrawIndexedPrimitiveUP)(struct IDirect3DDevice9*, uint32_t, int, uint32_t, uint32_t, uint32_t, const void*, uint32_t, const void*, uint32_t);
    int (__stdcall *ProcessVertices)(struct IDirect3DDevice9*, uint32_t, uint32_t, uint32_t, void*, void*, uint32_t);
    int (__stdcall *CreateVertexDeclaration)(struct IDirect3DDevice9*, const void*, void**);
    int (__stdcall *SetVertexDeclaration)(struct IDirect3DDevice9*, void*);
    int (__stdcall *GetVertexDeclaration)(struct IDirect3DDevice9*, void**);
    int (__stdcall *SetFVF)(struct IDirect3DDevice9*, uint32_t);
    int (__stdcall *GetFVF)(struct IDirect3DDevice9*, uint32_t*);
    int (__stdcall *CreateVertexShader)(struct IDirect3DDevice9*, const void*, void**);
    int (__stdcall *SetVertexShader)(struct IDirect3DDevice9*, void*);
    int (__stdcall *GetVertexShader)(struct IDirect3DDevice9*, void**);
    int (__stdcall *SetVertexShaderConstantF)(struct IDirect3DDevice9*, uint32_t, const float*, uint32_t);
    int (__stdcall *GetVertexShaderConstantF)(struct IDirect3DDevice9*, uint32_t, float*, uint32_t);
    int (__stdcall *SetVertexShaderConstantI)(struct IDirect3DDevice9*, uint32_t, const int*, uint32_t);
    int (__stdcall *GetVertexShaderConstantI)(struct IDirect3DDevice9*, uint32_t, int*, uint32_t);
    int (__stdcall *SetVertexShaderConstantB)(struct IDirect3DDevice9*, uint32_t, const int*, uint32_t);
    int (__stdcall *GetVertexShaderConstantB)(struct IDirect3DDevice9*, uint32_t, int*, uint32_t);
    int (__stdcall *SetStreamSource)(struct IDirect3DDevice9*, uint32_t, void*, uint32_t, uint32_t);
    int (__stdcall *GetStreamSource)(struct IDirect3DDevice9*, uint32_t, void**, uint32_t*, uint32_t*);
    int (__stdcall *SetStreamSourceFreq)(struct IDirect3DDevice9*, uint32_t, uint32_t);
    int (__stdcall *GetStreamSourceFreq)(struct IDirect3DDevice9*, uint32_t, uint32_t*);
    int (__stdcall *SetIndices)(struct IDirect3DDevice9*, void*);
    int (__stdcall *GetIndices)(struct IDirect3DDevice9*, void**);
    int (__stdcall *CreatePixelShader)(struct IDirect3DDevice9*, const void*, void**);
    int (__stdcall *SetPixelShader)(struct IDirect3DDevice9*, void*);
    int (__stdcall *GetPixelShader)(struct IDirect3DDevice9*, void**);
    int (__stdcall *SetPixelShaderConstantF)(struct IDirect3DDevice9*, uint32_t, const float*, uint32_t);
    int (__stdcall *GetPixelShaderConstantF)(struct IDirect3DDevice9*, uint32_t, float*, uint32_t);
    int (__stdcall *SetPixelShaderConstantI)(struct IDirect3DDevice9*, uint32_t, const int*, uint32_t);
    int (__stdcall *GetPixelShaderConstantI)(struct IDirect3DDevice9*, uint32_t, int*, uint32_t);
    int (__stdcall *SetPixelShaderConstantB)(struct IDirect3DDevice9*, uint32_t, const int*, uint32_t);
    int (__stdcall *GetPixelShaderConstantB)(struct IDirect3DDevice9*, uint32_t, int*, uint32_t);
    int (__stdcall *DrawRectPatch)(struct IDirect3DDevice9*, uint32_t, const float*, void*);
    int (__stdcall *DrawTriPatch)(struct IDirect3DDevice9*, uint32_t, const float*, void*);
    int (__stdcall *DeletePatch)(struct IDirect3DDevice9*, uint32_t);
    int (__stdcall *CreateQuery)(struct IDirect3DDevice9*, uint32_t, void**);
} IDirect3DDevice9Vtbl;

struct IDirect3DDevice9 {
    const IDirect3DDevice9Vtbl *lpVtbl;
};

/* Resource vtables (minimal) */
typedef struct IDirect3DVertexBuffer9Vtbl {
    int (__stdcall *QueryInterface)(struct IDirect3DVertexBuffer9*, const void*, void**);
    uint32_t (__stdcall *AddRef)(struct IDirect3DVertexBuffer9*);
    uint32_t (__stdcall *Release)(struct IDirect3DVertexBuffer9*);
    int (__stdcall *GetDevice)(struct IDirect3DVertexBuffer9*, struct IDirect3DDevice9**);
    int (__stdcall *SetPrivateData)(struct IDirect3DVertexBuffer9*, const void*, const void*, uint32_t, uint32_t);
    int (__stdcall *GetPrivateData)(struct IDirect3DVertexBuffer9*, const void*, void*, uint32_t*);
    int (__stdcall *FreePrivateData)(struct IDirect3DVertexBuffer9*, const void*);
    int (__stdcall *SetPriority)(struct IDirect3DVertexBuffer9*, uint32_t);
    int (__stdcall *GetPriority)(struct IDirect3DVertexBuffer9*, uint32_t*);
    int (__stdcall *PreLoad)(struct IDirect3DVertexBuffer9*);
    int (__stdcall *GetType)(struct IDirect3DVertexBuffer9*, uint32_t*);
    int (__stdcall *Lock)(struct IDirect3DVertexBuffer9*, uint32_t, uint32_t, void**, uint32_t);
    int (__stdcall *Unlock)(struct IDirect3DVertexBuffer9*);
    int (__stdcall *GetDesc)(struct IDirect3DVertexBuffer9*, void*);
} IDirect3DVertexBuffer9Vtbl;

struct IDirect3DVertexBuffer9 {
    const IDirect3DVertexBuffer9Vtbl *lpVtbl;
};

typedef struct IDirect3DIndexBuffer9Vtbl {
    int (__stdcall *QueryInterface)(struct IDirect3DIndexBuffer9*, const void*, void**);
    uint32_t (__stdcall *AddRef)(struct IDirect3DIndexBuffer9*);
    uint32_t (__stdcall *Release)(struct IDirect3DIndexBuffer9*);
    int (__stdcall *GetDevice)(struct IDirect3DIndexBuffer9*, struct IDirect3DDevice9**);
    int (__stdcall *SetPrivateData)(struct IDirect3DIndexBuffer9*, const void*, const void*, uint32_t, uint32_t);
    int (__stdcall *GetPrivateData)(struct IDirect3DIndexBuffer9*, const void*, void*, uint32_t*);
    int (__stdcall *FreePrivateData)(struct IDirect3DIndexBuffer9*, const void*);
    int (__stdcall *SetPriority)(struct IDirect3DIndexBuffer9*, uint32_t);
    int (__stdcall *GetPriority)(struct IDirect3DIndexBuffer9*, uint32_t*);
    int (__stdcall *PreLoad)(struct IDirect3DIndexBuffer9*);
    int (__stdcall *GetType)(struct IDirect3DIndexBuffer9*, uint32_t*);
    int (__stdcall *Lock)(struct IDirect3DIndexBuffer9*, uint32_t, uint32_t, void**, uint32_t);
    int (__stdcall *Unlock)(struct IDirect3DIndexBuffer9*);
    int (__stdcall *GetDesc)(struct IDirect3DIndexBuffer9*, void*);
} IDirect3DIndexBuffer9Vtbl;

struct IDirect3DIndexBuffer9 {
    const IDirect3DIndexBuffer9Vtbl *lpVtbl;
};

typedef struct IDirect3DTexture9Vtbl {
    int (__stdcall *QueryInterface)(struct IDirect3DTexture9*, const void*, void**);
    uint32_t (__stdcall *AddRef)(struct IDirect3DTexture9*);
    uint32_t (__stdcall *Release)(struct IDirect3DTexture9*);
    int (__stdcall *GetDevice)(struct IDirect3DTexture9*, struct IDirect3DDevice9**);
    int (__stdcall *SetPrivateData)(struct IDirect3DTexture9*, const void*, const void*, uint32_t, uint32_t);
    int (__stdcall *GetPrivateData)(struct IDirect3DTexture9*, const void*, void*, uint32_t*);
    int (__stdcall *FreePrivateData)(struct IDirect3DTexture9*, const void*);
    int (__stdcall *SetPriority)(struct IDirect3DTexture9*, uint32_t);
    int (__stdcall *GetPriority)(struct IDirect3DTexture9*, uint32_t*);
    int (__stdcall *PreLoad)(struct IDirect3DTexture9*);
    int (__stdcall *GetType)(struct IDirect3DTexture9*, uint32_t*);
    int (__stdcall *SetLOD)(struct IDirect3DTexture9*, uint32_t);
    int (__stdcall *GetLOD)(struct IDirect3DTexture9*, uint32_t*);
    int (__stdcall *GetLevelCount)(struct IDirect3DTexture9*, uint32_t*);
    int (__stdcall *SetAutoGenFilterType)(struct IDirect3DTexture9*, uint32_t);
    int (__stdcall *GetAutoGenFilterType)(struct IDirect3DTexture9*, uint32_t*);
    int (__stdcall *GenerateMipSubLevels)(struct IDirect3DTexture9*);
    int (__stdcall *GetLevelDesc)(struct IDirect3DTexture9*, uint32_t, void*);
    int (__stdcall *GetSurfaceLevel)(struct IDirect3DTexture9*, uint32_t, void**);
    int (__stdcall *LockRect)(struct IDirect3DTexture9*, uint32_t, void*, const void*, uint32_t);
    int (__stdcall *UnlockRect)(struct IDirect3DTexture9*, uint32_t);
    int (__stdcall *AddDirtyRect)(struct IDirect3DTexture9*, const void*);
} IDirect3DTexture9Vtbl;

struct IDirect3DTexture9 {
    const IDirect3DTexture9Vtbl *lpVtbl;
};

/* D3DPRESENT_PARAMETERS */
typedef struct D3DPRESENT_PARAMETERS {
    uint32_t BackBufferWidth;
    uint32_t BackBufferHeight;
    uint32_t BackBufferFormat;
    uint32_t BackBufferCount;
    uint32_t MultiSampleType;
    uint32_t MultiSampleQuality;
    uint32_t SwapEffect;
    void* hDeviceWindow;
    int Windowed;
    int EnableAutoDepthStencil;
    uint32_t AutoDepthStencilFormat;
    uint32_t Flags;
    uint32_t FullScreen_RefreshRateInHz;
    uint32_t PresentationInterval;
} D3DPRESENT_PARAMETERS;

/* D3DVERTEXBUFFER_DESC */
typedef struct D3DVERTEXBUFFER_DESC {
    uint32_t Format;
    uint32_t Type;
    uint32_t Usage;
    uint32_t Pool;
    uint32_t Size;
    uint32_t FVF;
} D3DVERTEXBUFFER_DESC;

/* D3DINDEXBUFFER_DESC */
typedef struct D3DINDEXBUFFER_DESC {
    uint32_t Format;
    uint32_t Type;
    uint32_t Usage;
    uint32_t Pool;
    uint32_t Size;
} D3DINDEXBUFFER_DESC;

/* D3DSURFACE_DESC */
typedef struct D3DSURFACE_DESC {
    uint32_t Format;
    uint32_t Type;
    uint32_t Usage;
    uint32_t Pool;
    uint32_t MultiSampleType;
    uint32_t MultiSampleQuality;
    uint32_t Width;
    uint32_t Height;
} D3DSURFACE_DESC;

/* D3DLOCKED_RECT */
typedef struct D3DLOCKED_RECT {
    int32_t Pitch;
    void* pBits;
} D3DLOCKED_RECT;

/* D3DVERTEXELEMENT9 */
typedef struct D3DVERTEXELEMENT9 {
    uint16_t Stream;
    uint16_t Offset;
    uint8_t Type;
    uint8_t Method;
    uint8_t Usage;
    uint8_t UsageIndex;
} D3DVERTEXELEMENT9;

/* D3DXMATRIX (4x4) */
typedef struct D3DXMATRIX {
    float _11, _12, _13, _14;
    float _21, _22, _23, _24;
    float _31, _32, _33, _34;
    float _41, _42, _43, _44;
} D3DXMATRIX;

/* D3DXVECTOR3 */
typedef struct D3DXVECTOR3 {
    float x, y, z;
} D3DXVECTOR3;

/* D3DXVECTOR4 */
typedef struct D3DXVECTOR4 {
    float x, y, z, w;
} D3DXVECTOR4;

/* D3DXQUATERNION */
typedef struct D3DXQUATERNION {
    float x, y, z, w;
} D3DXQUATERNION;

/* D3DVIEWPORT9 */
typedef struct D3DVIEWPORT9 {
    uint32_t X;
    uint32_t Y;
    uint32_t Width;
    uint32_t Height;
    float MinZ;
    float MaxZ;
} D3DVIEWPORT9;

/* D3DMATERIAL9 */
typedef struct D3DMATERIAL9 {
    struct { float r, g, b, a; } Diffuse;
    struct { float r, g, b, a; } Ambient;
    struct { float r, g, b, a; } Specular;
    struct { float r, g, b, a; } Emissive;
    float Power;
} D3DMATERIAL9;

/* D3DLIGHT9 */
typedef struct D3DLIGHT9 {
    uint32_t Type;
    struct { float r, g, b, a; } Diffuse;
    struct { float r, g, b, a; } Specular;
    struct { float r, g, b, a; } Ambient;
    struct { float x, y, z; } Position;
    struct { float x, y, z; } Direction;
    float Range;
    float Falloff;
    float Attenuation0;
    float Attenuation1;
    float Attenuation2;
    float Theta;
    float Phi;
} D3DLIGHT9;

/* D3DCLIPSTATUS9 */
typedef struct D3DCLIPSTATUS9 {
    uint32_t ClipUnion;
    uint32_t ClipIntersection;
} D3DCLIPSTATUS9;

/* IDirect3D9 creation function */
struct IDirect3D9* Direct3DCreate9(uint32_t SDKVersion);

/* Logging function */
void d3d9_log(const char* format, ...);

#ifdef __cplusplus
}
#endif

#endif /* D3D9_HLE_H */
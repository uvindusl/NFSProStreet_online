#ifndef D3DX9_HLE_H
#define D3DX9_HLE_H

#include <stdint.h>

/* __stdcall is Windows-specific; no-op on Linux */
#ifndef __stdcall
#define __stdcall
#endif

/* Include d3d9_hle.h for D3DXMATRIX, D3DXVECTOR3, D3DXVECTOR4, and D3D types */
#include "d3d9_hle.h"

#ifdef __cplusplus
extern "C" {
#endif

/* D3DX Matrix math functions */
D3DXMATRIX* __stdcall D3DXMatrixMultiply(D3DXMATRIX* pOut, const D3DXMATRIX* pM1, const D3DXMATRIX* pM2);
D3DXMATRIX* __stdcall D3DXMatrixMultiplyTranspose(D3DXMATRIX* pOut, const D3DXMATRIX* pM1, const D3DXMATRIX* pM2);
D3DXMATRIX* __stdcall D3DXMatrixTranspose(D3DXMATRIX* pOut, const D3DXMATRIX* pM);
D3DXMATRIX* __stdcall D3DXMatrixInverse(D3DXMATRIX* pOut, float* pDeterminant, const D3DXMATRIX* pM);

/* D3DX Vector math functions */
D3DXVECTOR4* __stdcall D3DXVec3Transform(D3DXVECTOR4* pOut, const D3DXVECTOR3* pV, const D3DXMATRIX* pM);
D3DXVECTOR4* __stdcall D3DXVec4Transform(D3DXVECTOR4* pOut, const D3DXVECTOR4* pV, const D3DXMATRIX* pM);

/* D3DX Effect functions */
int __stdcall D3DXCreateEffectFromResourceA(
    void* pDevice,
    void* hSrcModule,
    const char* pSrcResource,
    const void* pDefines,
    void* pInclude,
    uint32_t Flags,
    void* pPool,
    void** ppEffect,
    void** ppErrorBuffer
);

int __stdcall D3DXCreateEffectPool(void** ppPool);

/* D3DX Shader profile functions */
const char* __stdcall D3DXGetVertexShaderProfile(void* pDevice);
const char* __stdcall D3DXGetPixelShaderProfile(void* pDevice);

/* D3DX Texture creation functions */
int __stdcall D3DXCreateCubeTextureFromFileInMemory(
    void* pDevice,
    const void* pSrcData,
    uint32_t SrcDataSize,
    void** ppCubeTexture
);

int __stdcall D3DXCreateVolumeTextureFromFileInMemory(
    void* pDevice,
    const void* pSrcData,
    uint32_t SrcDataSize,
    void** ppVolumeTexture
);

/* D3DX Surface saving functions */
int __stdcall D3DXSaveSurfaceToFileA(
    const char* pDestFile,
    uint32_t DestFormat,
    void* pSrcSurface,
    const void* pSrcPalette,
    const void* pSrcRect
);

int __stdcall D3DXSaveSurfaceToFileInMemory(
    void** ppDestBuf,
    uint32_t DestFormat,
    void* pSrcSurface,
    const void* pSrcPalette,
    const void* pSrcRect
);

/* Logging */
void d3dx9_log(const char* format, ...);

#ifdef __cplusplus
}
#endif

#endif /* D3DX9_HLE_H */
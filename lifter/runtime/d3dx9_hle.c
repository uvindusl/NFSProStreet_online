#include "d3d9_hle.h"
#include "d3dx9_hle.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>

/* ===== Logging ===== */
static int g_d3dx9_log_enabled = 1;

void d3dx9_log(const char* format, ...) {
    if (!g_d3dx9_log_enabled) return;
    va_list args;
    va_start(args, format);
    fprintf(stderr, "[d3dx9] ");
    vfprintf(stderr, format, args);
    fprintf(stderr, "\n");
    va_end(args);
}

/* ===== Helper: float equality with epsilon ===== */
static inline int float_eq(float a, float b) {
    return fabsf(a - b) < 1e-6f;
}

/* ===== D3DXMatrixMultiply =====
 * pOut = pM1 * pM2
 * Standard row-major 4x4 matrix multiplication
 */
D3DXMATRIX* __stdcall D3DXMatrixMultiply(D3DXMATRIX* pOut, const D3DXMATRIX* pM1, const D3DXMATRIX* pM2) {
    d3dx9_log("D3DXMatrixMultiply(pOut=%p, pM1=%p, pM2=%p)", pOut, pM1, pM2);
    
    if (!pOut || !pM1 || !pM2) return NULL;
    
    const float* m1 = (const float*)pM1;
    const float* m2 = (const float*)pM2;
    float* out = (float*)pOut;
    
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            out[i*4 + j] = 
                m1[i*4 + 0] * m2[0*4 + j] +
                m1[i*4 + 1] * m2[1*4 + j] +
                m1[i*4 + 2] * m2[2*4 + j] +
                m1[i*4 + 3] * m2[3*4 + j];
        }
    }
    
    return pOut;
}

/* ===== D3DXMatrixMultiplyTranspose =====
 * pOut = (pM1 * pM2)^T = pM2^T * pM1^T
 */
D3DXMATRIX* __stdcall D3DXMatrixMultiplyTranspose(D3DXMATRIX* pOut, const D3DXMATRIX* pM1, const D3DXMATRIX* pM2) {
    d3dx9_log("D3DXMatrixMultiplyTranspose(pOut=%p, pM1=%p, pM2=%p)", pOut, pM1, pM2);
    
    if (!pOut || !pM1 || !pM2) return NULL;
    
    D3DXMATRIX temp;
    D3DXMatrixMultiply(&temp, pM1, pM2);
    return D3DXMatrixTranspose(pOut, &temp);
}

/* ===== D3DXMatrixTranspose =====
 * pOut = pM^T
 */
D3DXMATRIX* __stdcall D3DXMatrixTranspose(D3DXMATRIX* pOut, const D3DXMATRIX* pM) {
    d3dx9_log("D3DXMatrixTranspose(pOut=%p, pM=%p)", pOut, pM);
    
    if (!pOut || !pM) return NULL;
    
    const float* m = (const float*)pM;
    float* out = (float*)pOut;
    
    /* Use temporary array to handle in-place transpose correctly */
    float temp[16];
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            temp[i*4 + j] = m[j*4 + i];
        }
    }
    memcpy(out, temp, sizeof(float) * 16);
    
    return pOut;
}

/* ===== D3DXMatrixInverse =====
 * pOut = pM^-1
 * pDeterminant receives determinant of pM (can be NULL)
 */
D3DXMATRIX* __stdcall D3DXMatrixInverse(D3DXMATRIX* pOut, float* pDeterminant, const D3DXMATRIX* pM) {
    d3dx9_log("D3DXMatrixInverse(pOut=%p, pDeterminant=%p, pM=%p)", pOut, pDeterminant, pM);
    
    if (!pOut || !pM) return NULL;
    
    const float* m = (const float*)pM;
    float* out = (float*)pOut;
    
    /* Compute determinant using cofactor expansion */
    float det = 
        m[0] * (m[5]*(m[10]*m[15] - m[11]*m[14]) - m[6]*(m[9]*m[15] - m[11]*m[13]) + m[7]*(m[9]*m[14] - m[10]*m[13]))
      - m[1] * (m[4]*(m[10]*m[15] - m[11]*m[14]) - m[6]*(m[8]*m[15] - m[11]*m[12]) + m[7]*(m[8]*m[14] - m[10]*m[12]))
      + m[2] * (m[4]*(m[9]*m[15] - m[11]*m[13]) - m[5]*(m[8]*m[15] - m[11]*m[12]) + m[7]*(m[8]*m[13] - m[9]*m[12]))
      - m[3] * (m[4]*(m[9]*m[14] - m[10]*m[13]) - m[5]*(m[8]*m[14] - m[10]*m[12]) + m[6]*(m[8]*m[13] - m[9]*m[12]));
    
    if (pDeterminant) *pDeterminant = det;
    
    if (float_eq(det, 0.0f)) {
        /* Singular matrix - return zero matrix */
        memset(out, 0, sizeof(D3DXMATRIX));
        return NULL;
    }
    
    float inv_det = 1.0f / det;
    
    /* Compute adjugate matrix (transpose of cofactor matrix) */
    out[0] =  (m[5]*(m[10]*m[15] - m[11]*m[14]) - m[6]*(m[9]*m[15] - m[11]*m[13]) + m[7]*(m[9]*m[14] - m[10]*m[13])) * inv_det;
    out[1] = -(m[1]*(m[10]*m[15] - m[11]*m[14]) - m[2]*(m[9]*m[15] - m[11]*m[13]) + m[3]*(m[9]*m[14] - m[10]*m[13])) * inv_det;
    out[2] =  (m[1]*(m[6]*m[15] - m[7]*m[14]) - m[2]*(m[5]*m[15] - m[7]*m[13]) + m[3]*(m[5]*m[14] - m[6]*m[13])) * inv_det;
    out[3] = -(m[1]*(m[6]*m[11] - m[7]*m[10]) - m[2]*(m[5]*m[11] - m[7]*m[9]) + m[3]*(m[5]*m[10] - m[6]*m[9])) * inv_det;
    
    out[4] = -(m[4]*(m[10]*m[15] - m[11]*m[14]) - m[6]*(m[8]*m[15] - m[11]*m[12]) + m[7]*(m[8]*m[14] - m[10]*m[12])) * inv_det;
    out[5] =  (m[0]*(m[10]*m[15] - m[11]*m[14]) - m[2]*(m[8]*m[15] - m[11]*m[12]) + m[3]*(m[8]*m[14] - m[10]*m[12])) * inv_det;
    out[6] = -(m[0]*(m[6]*m[15] - m[7]*m[14]) - m[2]*(m[4]*m[15] - m[7]*m[12]) + m[3]*(m[4]*m[14] - m[6]*m[12])) * inv_det;
    out[7] =  (m[0]*(m[6]*m[11] - m[7]*m[10]) - m[2]*(m[4]*m[11] - m[7]*m[9]) + m[3]*(m[4]*m[10] - m[6]*m[9])) * inv_det;
    
    out[8] =  (m[4]*(m[9]*m[15] - m[11]*m[13]) - m[5]*(m[8]*m[15] - m[11]*m[12]) + m[7]*(m[8]*m[13] - m[9]*m[12])) * inv_det;
    out[9] = -(m[0]*(m[9]*m[15] - m[11]*m[13]) - m[1]*(m[8]*m[15] - m[11]*m[12]) + m[3]*(m[8]*m[13] - m[9]*m[12])) * inv_det;
    out[10] = (m[0]*(m[5]*m[15] - m[7]*m[13]) - m[1]*(m[4]*m[15] - m[7]*m[12]) + m[3]*(m[4]*m[13] - m[5]*m[12])) * inv_det;
    out[11] = -(m[0]*(m[5]*m[11] - m[7]*m[9]) - m[1]*(m[4]*m[11] - m[7]*m[8]) + m[3]*(m[4]*m[9] - m[5]*m[8])) * inv_det;
    
    out[12] = -(m[4]*(m[9]*m[14] - m[10]*m[13]) - m[5]*(m[8]*m[14] - m[10]*m[12]) + m[6]*(m[8]*m[13] - m[9]*m[12])) * inv_det;
    out[13] =  (m[0]*(m[9]*m[14] - m[10]*m[13]) - m[1]*(m[8]*m[14] - m[10]*m[12]) + m[2]*(m[8]*m[13] - m[9]*m[12])) * inv_det;
    out[14] = -(m[0]*(m[5]*m[14] - m[6]*m[13]) - m[1]*(m[4]*m[14] - m[6]*m[12]) + m[2]*(m[4]*m[13] - m[5]*m[12])) * inv_det;
    out[15] =  (m[0]*(m[5]*m[10] - m[6]*m[9]) - m[1]*(m[4]*m[10] - m[6]*m[8]) + m[2]*(m[4]*m[9] - m[5]*m[8])) * inv_det;
    
    return pOut;
}

/* ===== D3DXVec3Transform =====
 * Transforms a 3D vector by a matrix
 * pOut = (x,y,z,1) * pM
 */
D3DXVECTOR4* __stdcall D3DXVec3Transform(D3DXVECTOR4* pOut, const D3DXVECTOR3* pV, const D3DXMATRIX* pM) {
    d3dx9_log("D3DXVec3Transform(pOut=%p, pV=%p, pM=%p)", pOut, pV, pM);
    
    if (!pOut || !pV || !pM) return NULL;
    
    const float* m = (const float*)pM;
    
    pOut->x = pV->x * m[0] + pV->y * m[4] + pV->z * m[8] + m[12];
    pOut->y = pV->x * m[1] + pV->y * m[5] + pV->z * m[9] + m[13];
    pOut->z = pV->x * m[2] + pV->y * m[6] + pV->z * m[10] + m[14];
    pOut->w = pV->x * m[3] + pV->y * m[7] + pV->z * m[11] + m[15];
    
    return pOut;
}

/* ===== D3DXVec4Transform =====
 * Transforms a 4D vector by a matrix
 * pOut = (x,y,z,w) * pM
 */
D3DXVECTOR4* __stdcall D3DXVec4Transform(D3DXVECTOR4* pOut, const D3DXVECTOR4* pV, const D3DXMATRIX* pM) {
    d3dx9_log("D3DXVec4Transform(pOut=%p, pV=%p, pM=%p)", pOut, pV, pM);
    
    if (!pOut || !pV || !pM) return NULL;
    
    const float* m = (const float*)pM;
    
    pOut->x = pV->x * m[0] + pV->y * m[4] + pV->z * m[8] + pV->w * m[12];
    pOut->y = pV->x * m[1] + pV->y * m[5] + pV->z * m[9] + pV->w * m[13];
    pOut->z = pV->x * m[2] + pV->y * m[6] + pV->z * m[10] + pV->w * m[14];
    pOut->w = pV->x * m[3] + pV->y * m[7] + pV->z * m[11] + pV->w * m[15];
    
    return pOut;
}

/* ===== D3DXCreateEffectFromResourceA =====
 * Creates an effect from a resource - stub returns dummy effect
 */
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
) {
    d3dx9_log("D3DXCreateEffectFromResourceA(pDevice=%p, hSrcModule=%p, pSrcResource=%s, Flags=%u)", 
              pDevice, hSrcModule, pSrcResource ? pSrcResource : "NULL", Flags);
    
    if (ppEffect) {
        /* Return a fake effect pointer */
        *ppEffect = (void*)0x10000000;
    }
    if (ppErrorBuffer) {
        *ppErrorBuffer = NULL;
    }
    
    return D3D_OK;
}

/* ===== D3DXCreateEffectPool =====
 * Creates an effect pool - stub
 */
int __stdcall D3DXCreateEffectPool(void** ppPool) {
    d3dx9_log("D3DXCreateEffectPool(ppPool=%p)", ppPool);
    
    if (ppPool) {
        *ppPool = (void*)0x20000000;
    }
    
    return D3D_OK;
}

/* ===== D3DXGetVertexShaderProfile =====
 * Returns vertex shader profile string
 */
const char* __stdcall D3DXGetVertexShaderProfile(void* pDevice) {
    d3dx9_log("D3DXGetVertexShaderProfile(pDevice=%p)", pDevice);
    return "vs_2_0";
}

/* ===== D3DXGetPixelShaderProfile =====
 * Returns pixel shader profile string
 */
const char* __stdcall D3DXGetPixelShaderProfile(void* pDevice) {
    d3dx9_log("D3DXGetPixelShaderProfile(pDevice=%p)", pDevice);
    return "ps_2_0";
}

/* ===== D3DXCreateCubeTextureFromFileInMemory =====
 * Creates a cube texture from memory - stub
 */
int __stdcall D3DXCreateCubeTextureFromFileInMemory(
    void* pDevice,
    const void* pSrcData,
    uint32_t SrcDataSize,
    void** ppCubeTexture
) {
    d3dx9_log("D3DXCreateCubeTextureFromFileInMemory(pDevice=%p, pSrcData=%p, SrcDataSize=%u)", 
              pDevice, pSrcData, SrcDataSize);
    
    if (ppCubeTexture) {
        *ppCubeTexture = (void*)0x30000000;
    }
    
    return D3D_OK;
}

/* ===== D3DXCreateVolumeTextureFromFileInMemory =====
 * Creates a volume texture from memory - stub
 */
int __stdcall D3DXCreateVolumeTextureFromFileInMemory(
    void* pDevice,
    const void* pSrcData,
    uint32_t SrcDataSize,
    void** ppVolumeTexture
) {
    d3dx9_log("D3DXCreateVolumeTextureFromFileInMemory(pDevice=%p, pSrcData=%p, SrcDataSize=%u)", 
              pDevice, pSrcData, SrcDataSize);
    
    if (ppVolumeTexture) {
        *ppVolumeTexture = (void*)0x40000000;
    }
    
    return D3D_OK;
}

/* ===== D3DXSaveSurfaceToFileA =====
 * Saves a surface to file - stub
 */
int __stdcall D3DXSaveSurfaceToFileA(
    const char* pDestFile,
    uint32_t DestFormat,
    void* pSrcSurface,
    const void* pSrcPalette,
    const void* pSrcRect
) {
    d3dx9_log("D3DXSaveSurfaceToFileA(pDestFile=%s, DestFormat=%u, pSrcSurface=%p)", 
              pDestFile ? pDestFile : "NULL", DestFormat, pSrcSurface);
    
    return D3D_OK;
}

/* ===== D3DXSaveSurfaceToFileInMemory =====
 * Saves a surface to memory buffer - stub
 */
int __stdcall D3DXSaveSurfaceToFileInMemory(
    void** ppDestBuf,
    uint32_t DestFormat,
    void* pSrcSurface,
    const void* pSrcPalette,
    const void* pSrcRect
) {
    d3dx9_log("D3DXSaveSurfaceToFileInMemory(ppDestBuf=%p, DestFormat=%u, pSrcSurface=%p)", 
              ppDestBuf, DestFormat, pSrcSurface);
    
    if (ppDestBuf) {
        *ppDestBuf = (void*)0x50000000;
    }
    
    return D3D_OK;
}
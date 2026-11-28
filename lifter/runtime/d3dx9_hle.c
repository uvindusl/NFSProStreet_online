#include "d3d9_hle.h"
#include "d3dx9_hle.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>

/* ===== Logging =====
 * One line per D3DX math call: about 80 million lines (7 GB) in a race run,
 * which slowed the game down a lot.  Off unless PROSTREET_D3DX_LOG=1. */
static int g_d3dx9_log_enabled = -1;

void d3dx9_log(const char* format, ...) {
    if (g_d3dx9_log_enabled < 0) { const char *e = getenv("PROSTREET_D3DX_LOG"); g_d3dx9_log_enabled = e && *e == '1'; }
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
    
    /* The game multiplies in place (pOut == pM1 or pM2), which D3DX allows:
     * read both operands before writing anything. */
    float m1[16], m2[16];
    memcpy(m1, pM1, sizeof m1);
    memcpy(m2, pM2, sizeof m2);
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

    /* Gauss-Jordan with partial pivoting on a copy, so pOut may be pM. */
    double a[4][8], det = 1.0;
    const float* m = (const float*)pM;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) { a[i][j] = m[i*4 + j]; a[i][4 + j] = (i == j) ? 1.0 : 0.0; }
    for (int col = 0; col < 4; col++) {
        int piv = col;
        for (int r = col + 1; r < 4; r++)
            if (fabs(a[r][col]) > fabs(a[piv][col])) piv = r;
        if (a[piv][col] == 0.0) {
            if (pDeterminant) *pDeterminant = 0.0f;
            return NULL;                       /* singular: D3DX leaves pOut alone */
        }
        if (piv != col) {
            for (int j = 0; j < 8; j++) { double t = a[col][j]; a[col][j] = a[piv][j]; a[piv][j] = t; }
            det = -det;
        }
        det *= a[col][col];
        double inv = 1.0 / a[col][col];
        for (int j = 0; j < 8; j++) a[col][j] *= inv;
        for (int r = 0; r < 4; r++) {
            if (r == col) continue;
            double f = a[r][col];
            if (f != 0.0) for (int j = 0; j < 8; j++) a[r][j] -= f * a[col][j];
        }
    }
    if (pDeterminant) *pDeterminant = (float)det;
    float* out = (float*)pOut;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) out[i*4 + j] = (float)a[i][4 + j];
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
    const float x = pV->x, y = pV->y, z = pV->z;   /* pOut may be pV */
    
    pOut->x = x * m[0] + y * m[4] + z * m[8] + m[12];
    pOut->y = x * m[1] + y * m[5] + z * m[9] + m[13];
    pOut->z = x * m[2] + y * m[6] + z * m[10] + m[14];
    pOut->w = x * m[3] + y * m[7] + z * m[11] + m[15];
    
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
    const float x = pV->x, y = pV->y, z = pV->z, w = pV->w;   /* pOut may be pV */
    
    pOut->x = x * m[0] + y * m[4] + z * m[8] + w * m[12];
    pOut->y = x * m[1] + y * m[5] + z * m[9] + w * m[13];
    pOut->z = x * m[2] + y * m[6] + z * m[10] + w * m[14];
    pOut->w = x * m[3] + y * m[7] + z * m[11] + w * m[15];
    
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
        *ppPool = (void*)0x6e000000;
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
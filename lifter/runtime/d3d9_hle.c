#include "d3d9_hle.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/* ===== Logging ===== */
static int g_d3d9_log_enabled = 1;

void d3d9_log(const char* format, ...) {
    if (!g_d3d9_log_enabled) return;
    va_list args;
    va_start(args, format);
    fprintf(stderr, "[d3d9] ");
    vfprintf(stderr, format, args);
    fprintf(stderr, "\n");
    va_end(args);
}

/* ===== Reference counting base ===== */
typedef struct {
    uint32_t refcount;
} RefCounted;

/* All impl structs have vtbl (pointer) as first member, then RefCounted base */
#define IMPL_BASE(impl) ((RefCounted*)((char*)(impl) + sizeof(void*)))

static uint32_t ref_add(void* impl) {
    RefCounted* base = IMPL_BASE(impl);
    return ++base->refcount;
}

static uint32_t ref_release(void* impl) {
    RefCounted* base = IMPL_BASE(impl);
    if (base->refcount == 0) return 0;
    uint32_t r = --base->refcount;
    if (r == 0) {
        free(impl);
    }
    return r;
}

/* ===== Shader objects =====
 * CreateVertexShader/CreatePixelShader receive a pointer to compiled D3D9
 * bytecode (the same token streams ProStreet keeps in its .rsrc).  The runtime
 * needs that bytecode so it can translate it to WGSL (or run it on the CPU
 * interpreter); keep an exact copy plus the profile and instruction count, and
 * hand the guest an opaque handle.  Set NFSPROSTREET_SHADER_DIR=<dir> to also
 * dump every created shader there (game data: keep it out of the repo). */
typedef struct {
    const void* vtbl;
    RefCounted base;
    int id;
    int is_vs;
    int ninstr;
    uint32_t nbytes;
    uint8_t* bytes;
} ShaderImpl;

#define D3D9_MAX_SHADERS 8192
static ShaderImpl* g_shaders[D3D9_MAX_SHADERS];
static int g_shader_next_id = 0;
static int g_shader_dir_checked = 0;
static const char* g_shader_dir = NULL;

static const char* shader_dir(void) {
    if (!g_shader_dir_checked) {
        g_shader_dir = getenv("NFSPROSTREET_SHADER_DIR");
        g_shader_dir_checked = 1;
    }
    return g_shader_dir;
}

/* Walk a D3D9 token stream and return its byte length (up to and including the
 * END token), or 0 if it cannot be walked.  The instruction length is bits 27-24
 * (dwords after the token) in shader model 2+; comments use bits 30-16; shader
 * model 1 has no length field, so operands are scanned by their bit-31
 * parameter-token marker.  Mirrors lifter/tools/extract_shaders.py. */
static uint32_t d3d9_shader_scan(const uint32_t* tok, int* out_ninstr) {
    if (out_ninstr) *out_ninstr = 0;
    if (!tok) return 0;
    uint32_t ver = tok[0];
    if ((ver >> 16) != 0xFFFEu && (ver >> 16) != 0xFFFFu) return 0;
    unsigned major = (ver >> 8) & 0xFFu;
    uint32_t i = 1;
    int k = 0;
    while (i < 65536u) {
        uint32_t t = tok[i];
        if (t == 0x0000FFFFu) {                 /* END */
            if (out_ninstr) *out_ninstr = k;
            return (i + 1u) * 4u;
        }
        unsigned op = t & 0xFFFFu;
        if (op == 0xFFFEu) {                    /* comment: not an instruction */
            i += 1u + ((t >> 16) & 0x7FFFu);
            continue;
        }
        if (major >= 2u) {
            i += 1u + ((t >> 24) & 0xFu);
        } else {
            uint32_t j = i + 1u;                /* shader model 1: scan operands */
            while (j < 65536u && (tok[j] & 0x80000000u)) j++;
            if (j == i + 1u) return 0;
            i = j;
        }
        k++;
    }
    return 0;
}

static void shader_dump(const ShaderImpl* s) {
    const char* dir = shader_dir();
    if (!dir || !*dir) return;
    char path[512];
    snprintf(path, sizeof path, "%s/%s_%03d.bin", dir, s->is_vs ? "vs" : "ps", s->id);
    FILE* f = fopen(path, "wb");
    if (!f) return;
    fwrite(s->bytes, 1, s->nbytes, f);
    fclose(f);
}

static int __stdcall sh_QueryInterface(void* self, const void* riid, void** ppv) {
    (void)self; (void)riid;
    if (ppv) *ppv = NULL;
    return D3D_OK;
}

static uint32_t __stdcall sh_AddRef(void* self) {
    ShaderImpl* s = (ShaderImpl*)self;
    return ++s->base.refcount;
}

static uint32_t __stdcall sh_Release(void* self) {
    ShaderImpl* s = (ShaderImpl*)self;
    if (s->base.refcount == 0) return 0;
    uint32_t r = --s->base.refcount;
    if (r == 0) {
        if (s->id >= 0 && s->id < D3D9_MAX_SHADERS) g_shaders[s->id] = NULL;
        free(s->bytes);
        free(s);
    }
    return r;
}

static int __stdcall sh_GetDevice(void* self, void** ppDevice) {
    (void)self;
    if (ppDevice) *ppDevice = NULL;
    return D3D_OK;
}

static int __stdcall sh_GetFunction(void* self, void* pData, uint32_t* pSizeOfData) {
    ShaderImpl* s = (ShaderImpl*)self;
    if (pSizeOfData) {
        if (pData && *pSizeOfData >= s->nbytes) memcpy(pData, s->bytes, s->nbytes);
        *pSizeOfData = s->nbytes;
    }
    return D3D_OK;
}

/* IDirect3DVertexShader9 / IDirect3DPixelShader9 share one vtable shape:
 * QueryInterface, AddRef, Release, GetDevice, GetFunction. */
static const void* g_shader_vtbl[5] = {
    (const void*)sh_QueryInterface, (const void*)sh_AddRef, (const void*)sh_Release,
    (const void*)sh_GetDevice, (const void*)sh_GetFunction,
};

static int shader_create(const void* pFunction, void** ppShader, int is_vs) {
    if (ppShader) *ppShader = NULL;
    if (!pFunction) return D3DERR_INVALIDCALL;
    int ninstr = 0;
    uint32_t nbytes = d3d9_shader_scan((const uint32_t*)pFunction, &ninstr);
    if (nbytes == 0) {
        d3d9_log("  %s: bytecode could not be walked", is_vs ? "vs" : "ps");
        return D3DERR_INVALIDCALL;
    }
    ShaderImpl* s = calloc(1, sizeof *s);
    if (!s) return D3DERR_INVALIDCALL;
    s->bytes = malloc(nbytes);
    if (!s->bytes) { free(s); return D3DERR_INVALIDCALL; }
    memcpy(s->bytes, pFunction, nbytes);
    s->vtbl = g_shader_vtbl;
    s->base.refcount = 1;
    s->is_vs = is_vs;
    s->ninstr = ninstr;
    s->nbytes = nbytes;
    s->id = (g_shader_next_id < D3D9_MAX_SHADERS) ? g_shader_next_id++ : -1;
    if (s->id >= 0) g_shaders[s->id] = s;
    shader_dump(s);
    uint32_t ver = *(const uint32_t*)pFunction;
    d3d9_log("  %s_%u_%u: %u bytes, %d instructions, handle=%d",
             is_vs ? "vs" : "ps", (ver >> 8) & 0xFFu, ver & 0xFFu,
             nbytes, ninstr, s->id);
    if (ppShader) *ppShader = s;
    return D3D_OK;
}

/* ===== Resource implementations (vtbl must be first for COM layout) ===== */
typedef struct {
    const IDirect3DVertexBuffer9Vtbl* vtbl;
    RefCounted base;
    uint32_t size;
    uint32_t fvf;
    uint32_t usage;
    void* data;
} VertexBufferImpl;

typedef struct {
    const IDirect3DIndexBuffer9Vtbl* vtbl;
    RefCounted base;
    uint32_t size;
    uint32_t format;
    uint32_t usage;
    void* data;
} IndexBufferImpl;

typedef struct {
    const IDirect3DTexture9Vtbl* vtbl;
    RefCounted base;
    uint32_t width, height, levels;
    uint32_t usage, format, pool;
} TextureImpl;

/* ===== Vertex Buffer implementation ===== */
static int __stdcall vb_QueryInterface(struct IDirect3DVertexBuffer9* self, const void* riid, void** ppv) {
    (void)self; (void)riid;
    d3d9_log("IDirect3DVertexBuffer9::QueryInterface");
    if (ppv) *ppv = NULL;
    return D3D_OK;
}

static uint32_t __stdcall vb_AddRef(struct IDirect3DVertexBuffer9* self) {
    VertexBufferImpl* impl = (VertexBufferImpl*)self;
    return ref_add(impl);
}

static uint32_t __stdcall vb_Release(struct IDirect3DVertexBuffer9* self) {
    VertexBufferImpl* impl = (VertexBufferImpl*)self;
    return ref_release(impl);
}

static int __stdcall vb_GetDevice(struct IDirect3DVertexBuffer9* self, struct IDirect3DDevice9** ppDevice) {
    (void)self; (void)ppDevice;
    d3d9_log("IDirect3DVertexBuffer9::GetDevice");
    return D3D_OK;
}

static int __stdcall vb_SetPrivateData(struct IDirect3DVertexBuffer9* self, const void* refguid, const void* pData, uint32_t SizeOfData, uint32_t Flags) {
    (void)self; (void)refguid; (void)pData; (void)SizeOfData; (void)Flags;
    d3d9_log("IDirect3DVertexBuffer9::SetPrivateData");
    return D3D_OK;
}

static int __stdcall vb_GetPrivateData(struct IDirect3DVertexBuffer9* self, const void* refguid, void* pData, uint32_t* pSizeOfData) {
    (void)self; (void)refguid; (void)pData; (void)pSizeOfData;
    d3d9_log("IDirect3DVertexBuffer9::GetPrivateData");
    return D3D_OK;
}

static int __stdcall vb_FreePrivateData(struct IDirect3DVertexBuffer9* self, const void* refguid) {
    (void)self; (void)refguid;
    d3d9_log("IDirect3DVertexBuffer9::FreePrivateData");
    return D3D_OK;
}

static int __stdcall vb_SetPriority(struct IDirect3DVertexBuffer9* self, uint32_t PriorityNew) {
    (void)self; (void)PriorityNew;
    d3d9_log("IDirect3DVertexBuffer9::SetPriority");
    return D3D_OK;
}

static int __stdcall vb_GetPriority(struct IDirect3DVertexBuffer9* self, uint32_t* pPriorityNew) {
    (void)self; (void)pPriorityNew;
    d3d9_log("IDirect3DVertexBuffer9::GetPriority");
    return D3D_OK;
}

static int __stdcall vb_PreLoad(struct IDirect3DVertexBuffer9* self) {
    (void)self;
    d3d9_log("IDirect3DVertexBuffer9::PreLoad");
    return D3D_OK;
}

static int __stdcall vb_GetType(struct IDirect3DVertexBuffer9* self, uint32_t* pType) {
    (void)self;
    d3d9_log("IDirect3DVertexBuffer9::GetType");
    if (pType) *pType = D3DRTYPE_VERTEXBUFFER;
    return D3D_OK;
}

static int __stdcall vb_Lock(struct IDirect3DVertexBuffer9* self, uint32_t OffsetToLock, uint32_t SizeToLock, void** ppbData, uint32_t Flags) {
    VertexBufferImpl* impl = (VertexBufferImpl*)self;
    d3d9_log("IDirect3DVertexBuffer9::Lock(Offset=%u, Size=%u, Flags=%u)", OffsetToLock, SizeToLock, Flags);
    if (ppbData) {
        if (!impl->data && impl->size > 0) {
            impl->data = malloc(impl->size);
        }
        *ppbData = impl->data ? (char*)impl->data + OffsetToLock : NULL;
    }
    return D3D_OK;
}

static int __stdcall vb_Unlock(struct IDirect3DVertexBuffer9* self) {
    (void)self;
    d3d9_log("IDirect3DVertexBuffer9::Unlock");
    return D3D_OK;
}

static int __stdcall vb_GetDesc(struct IDirect3DVertexBuffer9* self, void* pDesc) {
    VertexBufferImpl* impl = (VertexBufferImpl*)self;
    d3d9_log("IDirect3DVertexBuffer9::GetDesc");
    if (pDesc) {
        D3DVERTEXBUFFER_DESC* desc = (D3DVERTEXBUFFER_DESC*)pDesc;
        desc->Format = 0;
        desc->Type = D3DRTYPE_VERTEXBUFFER;
        desc->Usage = impl->usage;
        desc->Pool = D3DPOOL_DEFAULT;
        desc->Size = impl->size;
        desc->FVF = impl->fvf;
    }
    return D3D_OK;
}

static const IDirect3DVertexBuffer9Vtbl g_vb_vtbl = {
    .QueryInterface = vb_QueryInterface,
    .AddRef = vb_AddRef,
    .Release = vb_Release,
    .GetDevice = vb_GetDevice,
    .SetPrivateData = vb_SetPrivateData,
    .GetPrivateData = vb_GetPrivateData,
    .FreePrivateData = vb_FreePrivateData,
    .SetPriority = vb_SetPriority,
    .GetPriority = vb_GetPriority,
    .PreLoad = vb_PreLoad,
    .GetType = vb_GetType,
    .Lock = vb_Lock,
    .Unlock = vb_Unlock,
    .GetDesc = vb_GetDesc,
};

/* ===== Index Buffer implementation ===== */
static int __stdcall ib_QueryInterface(struct IDirect3DIndexBuffer9* self, const void* riid, void** ppv) {
    (void)self; (void)riid;
    d3d9_log("IDirect3DIndexBuffer9::QueryInterface");
    if (ppv) *ppv = NULL;
    return D3D_OK;
}

static uint32_t __stdcall ib_AddRef(struct IDirect3DIndexBuffer9* self) {
    IndexBufferImpl* impl = (IndexBufferImpl*)self;
    return ref_add(impl);
}

static uint32_t __stdcall ib_Release(struct IDirect3DIndexBuffer9* self) {
    IndexBufferImpl* impl = (IndexBufferImpl*)self;
    return ref_release(impl);
}

static int __stdcall ib_GetDevice(struct IDirect3DIndexBuffer9* self, struct IDirect3DDevice9** ppDevice) {
    (void)self; (void)ppDevice;
    d3d9_log("IDirect3DIndexBuffer9::GetDevice");
    return D3D_OK;
}

static int __stdcall ib_SetPrivateData(struct IDirect3DIndexBuffer9* self, const void* refguid, const void* pData, uint32_t SizeOfData, uint32_t Flags) {
    (void)self; (void)refguid; (void)pData; (void)SizeOfData; (void)Flags;
    d3d9_log("IDirect3DIndexBuffer9::SetPrivateData");
    return D3D_OK;
}

static int __stdcall ib_GetPrivateData(struct IDirect3DIndexBuffer9* self, const void* refguid, void* pData, uint32_t* pSizeOfData) {
    (void)self; (void)refguid; (void)pData; (void)pSizeOfData;
    d3d9_log("IDirect3DIndexBuffer9::GetPrivateData");
    return D3D_OK;
}

static int __stdcall ib_FreePrivateData(struct IDirect3DIndexBuffer9* self, const void* refguid) {
    (void)self; (void)refguid;
    d3d9_log("IDirect3DIndexBuffer9::FreePrivateData");
    return D3D_OK;
}

static int __stdcall ib_SetPriority(struct IDirect3DIndexBuffer9* self, uint32_t PriorityNew) {
    (void)self; (void)PriorityNew;
    d3d9_log("IDirect3DIndexBuffer9::SetPriority");
    return D3D_OK;
}

static int __stdcall ib_GetPriority(struct IDirect3DIndexBuffer9* self, uint32_t* pPriorityNew) {
    (void)self; (void)pPriorityNew;
    d3d9_log("IDirect3DIndexBuffer9::GetPriority");
    return D3D_OK;
}

static int __stdcall ib_PreLoad(struct IDirect3DIndexBuffer9* self) {
    (void)self;
    d3d9_log("IDirect3DIndexBuffer9::PreLoad");
    return D3D_OK;
}

static int __stdcall ib_GetType(struct IDirect3DIndexBuffer9* self, uint32_t* pType) {
    (void)self;
    d3d9_log("IDirect3DIndexBuffer9::GetType");
    if (pType) *pType = D3DRTYPE_INDEXBUFFER;
    return D3D_OK;
}

static int __stdcall ib_Lock(struct IDirect3DIndexBuffer9* self, uint32_t OffsetToLock, uint32_t SizeToLock, void** ppbData, uint32_t Flags) {
    IndexBufferImpl* impl = (IndexBufferImpl*)self;
    d3d9_log("IDirect3DIndexBuffer9::Lock(Offset=%u, Size=%u, Flags=%u)", OffsetToLock, SizeToLock, Flags);
    if (ppbData) {
        if (!impl->data && impl->size > 0) {
            impl->data = malloc(impl->size);
        }
        *ppbData = impl->data ? (char*)impl->data + OffsetToLock : NULL;
    }
    return D3D_OK;
}

static int __stdcall ib_Unlock(struct IDirect3DIndexBuffer9* self) {
    (void)self;
    d3d9_log("IDirect3DIndexBuffer9::Unlock");
    return D3D_OK;
}

static int __stdcall ib_GetDesc(struct IDirect3DIndexBuffer9* self, void* pDesc) {
    IndexBufferImpl* impl = (IndexBufferImpl*)self;
    d3d9_log("IDirect3DIndexBuffer9::GetDesc");
    if (pDesc) {
        D3DINDEXBUFFER_DESC* desc = (D3DINDEXBUFFER_DESC*)pDesc;
        desc->Format = impl->format;
        desc->Type = D3DRTYPE_INDEXBUFFER;
        desc->Usage = impl->usage;
        desc->Pool = D3DPOOL_DEFAULT;
        desc->Size = impl->size;
    }
    return D3D_OK;
}

static const IDirect3DIndexBuffer9Vtbl g_ib_vtbl = {
    .QueryInterface = ib_QueryInterface,
    .AddRef = ib_AddRef,
    .Release = ib_Release,
    .GetDevice = ib_GetDevice,
    .SetPrivateData = ib_SetPrivateData,
    .GetPrivateData = ib_GetPrivateData,
    .FreePrivateData = ib_FreePrivateData,
    .SetPriority = ib_SetPriority,
    .GetPriority = ib_GetPriority,
    .PreLoad = ib_PreLoad,
    .GetType = ib_GetType,
    .Lock = ib_Lock,
    .Unlock = ib_Unlock,
    .GetDesc = ib_GetDesc,
};

/* ===== Texture implementation ===== */
static int __stdcall tex_QueryInterface(struct IDirect3DTexture9* self, const void* riid, void** ppv) {
    (void)self; (void)riid;
    d3d9_log("IDirect3DTexture9::QueryInterface");
    if (ppv) *ppv = NULL;
    return D3D_OK;
}

static uint32_t __stdcall tex_AddRef(struct IDirect3DTexture9* self) {
    TextureImpl* impl = (TextureImpl*)self;
    return ref_add(impl);
}

static uint32_t __stdcall tex_Release(struct IDirect3DTexture9* self) {
    TextureImpl* impl = (TextureImpl*)self;
    return ref_release(impl);
}

static int __stdcall tex_GetDevice(struct IDirect3DTexture9* self, struct IDirect3DDevice9** ppDevice) {
    (void)self; (void)ppDevice;
    d3d9_log("IDirect3DTexture9::GetDevice");
    return D3D_OK;
}

static int __stdcall tex_SetPrivateData(struct IDirect3DTexture9* self, const void* refguid, const void* pData, uint32_t SizeOfData, uint32_t Flags) {
    (void)self; (void)refguid; (void)pData; (void)SizeOfData; (void)Flags;
    d3d9_log("IDirect3DTexture9::SetPrivateData");
    return D3D_OK;
}

static int __stdcall tex_GetPrivateData(struct IDirect3DTexture9* self, const void* refguid, void* pData, uint32_t* pSizeOfData) {
    (void)self; (void)refguid; (void)pData; (void)pSizeOfData;
    d3d9_log("IDirect3DTexture9::GetPrivateData");
    return D3D_OK;
}

static int __stdcall tex_FreePrivateData(struct IDirect3DTexture9* self, const void* refguid) {
    (void)self; (void)refguid;
    d3d9_log("IDirect3DTexture9::FreePrivateData");
    return D3D_OK;
}

static int __stdcall tex_SetPriority(struct IDirect3DTexture9* self, uint32_t PriorityNew) {
    (void)self; (void)PriorityNew;
    d3d9_log("IDirect3DTexture9::SetPriority");
    return D3D_OK;
}

static int __stdcall tex_GetPriority(struct IDirect3DTexture9* self, uint32_t* pPriorityNew) {
    (void)self; (void)pPriorityNew;
    d3d9_log("IDirect3DTexture9::GetPriority");
    return D3D_OK;
}

static int __stdcall tex_PreLoad(struct IDirect3DTexture9* self) {
    (void)self;
    d3d9_log("IDirect3DTexture9::PreLoad");
    return D3D_OK;
}

static int __stdcall tex_GetType(struct IDirect3DTexture9* self, uint32_t* pType) {
    (void)self;
    d3d9_log("IDirect3DTexture9::GetType");
    if (pType) *pType = D3DRTYPE_TEXTURE;
    return D3D_OK;
}

static int __stdcall tex_SetLOD(struct IDirect3DTexture9* self, uint32_t LODNew) {
    (void)self; (void)LODNew;
    d3d9_log("IDirect3DTexture9::SetLOD");
    return D3D_OK;
}

static int __stdcall tex_GetLOD(struct IDirect3DTexture9* self, uint32_t* pLODNew) {
    (void)self; (void)pLODNew;
    d3d9_log("IDirect3DTexture9::GetLOD");
    return D3D_OK;
}

static int __stdcall tex_GetLevelCount(struct IDirect3DTexture9* self, uint32_t* pLevelCount) {
    TextureImpl* impl = (TextureImpl*)self;
    d3d9_log("IDirect3DTexture9::GetLevelCount");
    if (pLevelCount) *pLevelCount = impl->levels;
    return D3D_OK;
}

static int __stdcall tex_SetAutoGenFilterType(struct IDirect3DTexture9* self, uint32_t FilterType) {
    (void)self; (void)FilterType;
    d3d9_log("IDirect3DTexture9::SetAutoGenFilterType");
    return D3D_OK;
}

static int __stdcall tex_GetAutoGenFilterType(struct IDirect3DTexture9* self, uint32_t* pFilterType) {
    (void)self; (void)pFilterType;
    d3d9_log("IDirect3DTexture9::GetAutoGenFilterType");
    return D3D_OK;
}

static int __stdcall tex_GenerateMipSubLevels(struct IDirect3DTexture9* self) {
    (void)self;
    d3d9_log("IDirect3DTexture9::GenerateMipSubLevels");
    return D3D_OK;
}

static int __stdcall tex_GetLevelDesc(struct IDirect3DTexture9* self, uint32_t Level, void* pDesc) {
    TextureImpl* impl = (TextureImpl*)self;
    d3d9_log("IDirect3DTexture9::GetLevelDesc(Level=%u)", Level);
    if (pDesc) {
        D3DSURFACE_DESC* desc = (D3DSURFACE_DESC*)pDesc;
        uint32_t w = impl->width >> Level;
        uint32_t h = impl->height >> Level;
        if (w == 0) w = 1;
        if (h == 0) h = 1;
        desc->Format = impl->format;
        desc->Type = D3DRTYPE_TEXTURE;
        desc->Usage = impl->usage;
        desc->Pool = impl->pool;
        desc->MultiSampleType = D3DMULTISAMPLE_NONE;
        desc->MultiSampleQuality = 0;
        desc->Width = w;
        desc->Height = h;
    }
    return D3D_OK;
}

static int __stdcall tex_GetSurfaceLevel(struct IDirect3DTexture9* self, uint32_t Level, void** ppSurfaceLevel) {
    (void)self; (void)Level; (void)ppSurfaceLevel;
    d3d9_log("IDirect3DTexture9::GetSurfaceLevel");
    return D3D_OK;
}

static int __stdcall tex_LockRect(struct IDirect3DTexture9* self, uint32_t Level, void* pLockedRect, const void* pRect, uint32_t Flags) {
    (void)self; (void)Level; (void)pLockedRect; (void)pRect; (void)Flags;
    d3d9_log("IDirect3DTexture9::LockRect");
    return D3D_OK;
}

static int __stdcall tex_UnlockRect(struct IDirect3DTexture9* self, uint32_t Level) {
    (void)self; (void)Level;
    d3d9_log("IDirect3DTexture9::UnlockRect");
    return D3D_OK;
}

static int __stdcall tex_AddDirtyRect(struct IDirect3DTexture9* self, const void* pDirtyRect) {
    (void)self; (void)pDirtyRect;
    d3d9_log("IDirect3DTexture9::AddDirtyRect");
    return D3D_OK;
}

static const IDirect3DTexture9Vtbl g_tex_vtbl = {
    .QueryInterface = tex_QueryInterface,
    .AddRef = tex_AddRef,
    .Release = tex_Release,
    .GetDevice = tex_GetDevice,
    .SetPrivateData = tex_SetPrivateData,
    .GetPrivateData = tex_GetPrivateData,
    .FreePrivateData = tex_FreePrivateData,
    .SetPriority = tex_SetPriority,
    .GetPriority = tex_GetPriority,
    .PreLoad = tex_PreLoad,
    .GetType = tex_GetType,
    .SetLOD = tex_SetLOD,
    .GetLOD = tex_GetLOD,
    .GetLevelCount = tex_GetLevelCount,
    .SetAutoGenFilterType = tex_SetAutoGenFilterType,
    .GetAutoGenFilterType = tex_GetAutoGenFilterType,
    .GenerateMipSubLevels = tex_GenerateMipSubLevels,
    .GetLevelDesc = tex_GetLevelDesc,
    .GetSurfaceLevel = tex_GetSurfaceLevel,
    .LockRect = tex_LockRect,
    .UnlockRect = tex_UnlockRect,
    .AddDirtyRect = tex_AddDirtyRect,
};

/* ===== IDirect3D9 implementation ===== */
typedef struct {
    const IDirect3D9Vtbl* vtbl;
    RefCounted base;
} D3D9Impl;

static int __stdcall d3d9_QueryInterface(struct IDirect3D9* self, const void* riid, void** ppv) {
    (void)self; (void)riid;
    d3d9_log("IDirect3D9::QueryInterface");
    if (ppv) *ppv = NULL;
    return D3D_OK;
}

static uint32_t __stdcall d3d9_AddRef(struct IDirect3D9* self) {
    D3D9Impl* impl = (D3D9Impl*)self;
    uint32_t r = ref_add(impl);
    d3d9_log("IDirect3D9::AddRef -> %u", r);
    return r;
}

static uint32_t __stdcall d3d9_Release(struct IDirect3D9* self) {
    D3D9Impl* impl = (D3D9Impl*)self;
    uint32_t r = ref_release(impl);
    d3d9_log("IDirect3D9::Release -> %u", r);
    return r;
}

static int __stdcall d3d9_RegisterSoftwareDevice(struct IDirect3D9* self, void* pInitializeFunction) {
    (void)self; (void)pInitializeFunction;
    d3d9_log("IDirect3D9::RegisterSoftwareDevice");
    return D3D_OK;
}

static uint32_t __stdcall d3d9_GetAdapterCount(struct IDirect3D9* self) {
    (void)self;
    d3d9_log("IDirect3D9::GetAdapterCount -> 1");
    return 1;
}

static int __stdcall d3d9_GetAdapterIdentifier(struct IDirect3D9* self, uint32_t Adapter, uint32_t Flags, void* pIdentifier) {
    (void)self; (void)Adapter; (void)Flags; (void)pIdentifier;
    d3d9_log("IDirect3D9::GetAdapterIdentifier(Adapter=%u, Flags=%u)", Adapter, Flags);
    return D3D_OK;
}

static uint32_t __stdcall d3d9_GetAdapterModeCount(struct IDirect3D9* self, uint32_t Adapter, uint32_t Format) {
    (void)self; (void)Adapter; (void)Format;
    d3d9_log("IDirect3D9::GetAdapterModeCount(Adapter=%u, Format=%u)", Adapter, Format);
    return 1;
}

static int __stdcall d3d9_EnumAdapterModes(struct IDirect3D9* self, uint32_t Adapter, uint32_t Format, uint32_t Mode, void* pMode) {
    (void)self; (void)Adapter; (void)Format; (void)Mode; (void)pMode;
    d3d9_log("IDirect3D9::EnumAdapterModes(Adapter=%u, Format=%u, Mode=%u)", Adapter, Format, Mode);
    return D3D_OK;
}

static int __stdcall d3d9_GetAdapterDisplayMode(struct IDirect3D9* self, uint32_t Adapter, void* pMode) {
    (void)self;
    d3d9_log("IDirect3D9::GetAdapterDisplayMode(Adapter=%u)", Adapter);
    if (pMode) {
        uint32_t* mode = (uint32_t*)pMode;
        mode[0] = 1280;  /* Width */
        mode[1] = 720;   /* Height */
        mode[2] = 60;    /* RefreshRate */
        mode[3] = D3DFMT_X8R8G8B8;  /* Format */
    }
    return D3D_OK;
}

static int __stdcall d3d9_CheckDeviceType(struct IDirect3D9* self, uint32_t Adapter, uint32_t DevType, uint32_t AdapterFormat, uint32_t BackBufferFormat, int bWindowed) {
    (void)self;
    d3d9_log("IDirect3D9::CheckDeviceType(Adapter=%u, DevType=%u, AdapterFormat=%u, BackBufferFormat=%u, Windowed=%d)",
             Adapter, DevType, AdapterFormat, BackBufferFormat, bWindowed);
    return D3D_OK;
}

static int __stdcall d3d9_CheckDeviceFormat(struct IDirect3D9* self, uint32_t Adapter, uint32_t DeviceType, uint32_t Usage, uint32_t RType, uint32_t CheckFormat, uint32_t Pool) {
    (void)self; (void)Adapter; (void)DeviceType; (void)Usage; (void)RType; (void)CheckFormat; (void)Pool;
    d3d9_log("IDirect3D9::CheckDeviceFormat");
    return D3D_OK;
}

static int __stdcall d3d9_CheckDeviceMultiSampleType(struct IDirect3D9* self, uint32_t Adapter, uint32_t DeviceType, uint32_t SurfaceFormat, int Windowed, uint32_t MultiSampleType, uint32_t* pQualityLevels) {
    (void)self; (void)Adapter; (void)DeviceType; (void)SurfaceFormat; (void)Windowed; (void)MultiSampleType;
    d3d9_log("IDirect3D9::CheckDeviceMultiSampleType");
    if (pQualityLevels) *pQualityLevels = 0;
    return D3D_OK;
}

static int __stdcall d3d9_CheckDepthStencilMatch(struct IDirect3D9* self, uint32_t Adapter, uint32_t DeviceType, uint32_t AdapterFormat, uint32_t RenderTargetFormat, uint32_t DepthStencilFormat) {
    (void)self; (void)Adapter; (void)DeviceType; (void)AdapterFormat; (void)RenderTargetFormat; (void)DepthStencilFormat;
    d3d9_log("IDirect3D9::CheckDepthStencilMatch");
    return D3D_OK;
}

static int __stdcall d3d9_CheckDeviceFormatConversion(struct IDirect3D9* self, uint32_t Adapter, uint32_t DeviceType, uint32_t SourceFormat, uint32_t TargetFormat) {
    (void)self; (void)Adapter; (void)DeviceType; (void)SourceFormat; (void)TargetFormat;
    d3d9_log("IDirect3D9::CheckDeviceFormatConversion");
    return D3D_OK;
}

static int __stdcall d3d9_GetDeviceCaps(struct IDirect3D9* self, uint32_t Adapter, uint32_t DeviceType, void* pCaps) {
    (void)self; (void)Adapter; (void)DeviceType; (void)pCaps;
    d3d9_log("IDirect3D9::GetDeviceCaps");
    return D3D_OK;
}

static void* __stdcall d3d9_GetAdapterMonitor(struct IDirect3D9* self, uint32_t Adapter) {
    (void)self; (void)Adapter;
    d3d9_log("IDirect3D9::GetAdapterMonitor");
    return (void*)0x1;
}

static int __stdcall d3d9_CreateDevice(struct IDirect3D9* self, uint32_t Adapter, uint32_t DeviceType, void* hFocusWindow, uint32_t BehaviorFlags, void* pPresentationParameters, struct IDirect3DDevice9** ppReturnedDeviceInterface);

static const IDirect3D9Vtbl g_d3d9_vtbl = {
    .QueryInterface = d3d9_QueryInterface,
    .AddRef = d3d9_AddRef,
    .Release = d3d9_Release,
    .RegisterSoftwareDevice = d3d9_RegisterSoftwareDevice,
    .GetAdapterCount = d3d9_GetAdapterCount,
    .GetAdapterIdentifier = d3d9_GetAdapterIdentifier,
    .GetAdapterModeCount = d3d9_GetAdapterModeCount,
    .EnumAdapterModes = d3d9_EnumAdapterModes,
    .GetAdapterDisplayMode = d3d9_GetAdapterDisplayMode,
    .CheckDeviceType = d3d9_CheckDeviceType,
    .CheckDeviceFormat = d3d9_CheckDeviceFormat,
    .CheckDeviceMultiSampleType = d3d9_CheckDeviceMultiSampleType,
    .CheckDepthStencilMatch = d3d9_CheckDepthStencilMatch,
    .CheckDeviceFormatConversion = d3d9_CheckDeviceFormatConversion,
    .GetDeviceCaps = d3d9_GetDeviceCaps,
    .GetAdapterMonitor = d3d9_GetAdapterMonitor,
    .CreateDevice = d3d9_CreateDevice,
};

struct IDirect3D9* Direct3DCreate9(uint32_t SDKVersion) {
    d3d9_log("Direct3DCreate9(SDKVersion=%u)", SDKVersion);
    D3D9Impl* impl = calloc(1, sizeof(D3D9Impl));
    if (!impl) return NULL;
    impl->vtbl = &g_d3d9_vtbl;
    impl->base.refcount = 1;
    return (struct IDirect3D9*)impl;
}

/* ===== IDirect3DDevice9 implementation ===== */
typedef struct {
    const IDirect3DDevice9Vtbl* vtbl;
    RefCounted base;
    struct IDirect3D9* parent_d3d9;
    D3DPRESENT_PARAMETERS present_params;
    uint32_t fvf;
    struct IDirect3DVertexBuffer9* stream_source[8];
    struct IDirect3DIndexBuffer9* index_buffer;
    uint32_t render_state[256];
    D3DXMATRIX transforms[256];
    D3DVIEWPORT9 viewport;
    struct IDirect3DTexture9* textures[8];
    void* bound_vs;
    void* bound_ps;
} DeviceImpl;

static int __stdcall dev_QueryInterface(struct IDirect3DDevice9* self, const void* riid, void** ppv) {
    (void)self; (void)riid;
    d3d9_log("IDirect3DDevice9::QueryInterface");
    if (ppv) *ppv = NULL;
    return D3D_OK;
}

static uint32_t __stdcall dev_AddRef(struct IDirect3DDevice9* self) {
    DeviceImpl* impl = (DeviceImpl*)self;
    uint32_t r = ref_add(impl);
    d3d9_log("IDirect3DDevice9::AddRef -> %u", r);
    return r;
}

static uint32_t __stdcall dev_Release(struct IDirect3DDevice9* self) {
    DeviceImpl* impl = (DeviceImpl*)self;
    uint32_t r = ref_release(impl);
    d3d9_log("IDirect3DDevice9::Release -> %u", r);
    return r;
}

static int __stdcall dev_TestCooperativeLevel(struct IDirect3DDevice9* self) {
    (void)self;
    d3d9_log("IDirect3DDevice9::TestCooperativeLevel");
    return D3D_OK;
}

static uint32_t __stdcall dev_GetAvailableTextureMem(struct IDirect3DDevice9* self) {
    (void)self;
    d3d9_log("IDirect3DDevice9::GetAvailableTextureMem");
    return 0xFFFFFFFF;
}

static int __stdcall dev_EvictManagedResources(struct IDirect3DDevice9* self) {
    (void)self;
    d3d9_log("IDirect3DDevice9::EvictManagedResources");
    return D3D_OK;
}

static int __stdcall dev_GetDirect3D(struct IDirect3DDevice9* self, struct IDirect3D9** ppD3D9) {
    DeviceImpl* impl = (DeviceImpl*)self;
    d3d9_log("IDirect3DDevice9::GetDirect3D");
    if (ppD3D9 && impl->parent_d3d9) {
        impl->parent_d3d9->lpVtbl->AddRef(impl->parent_d3d9);
        *ppD3D9 = impl->parent_d3d9;
    }
    return D3D_OK;
}

static int __stdcall dev_GetDeviceCaps(struct IDirect3DDevice9* self, void* pCaps) {
    (void)self; (void)pCaps;
    d3d9_log("IDirect3DDevice9::GetDeviceCaps");
    return D3D_OK;
}

static int __stdcall dev_GetDisplayMode(struct IDirect3DDevice9* self, uint32_t iSwapChain, void* pMode) {
    (void)self; (void)iSwapChain; (void)pMode;
    d3d9_log("IDirect3DDevice9::GetDisplayMode");
    return D3D_OK;
}

static int __stdcall dev_GetCreationParameters(struct IDirect3DDevice9* self, void* pParameters) {
    (void)self; (void)pParameters;
    d3d9_log("IDirect3DDevice9::GetCreationParameters");
    return D3D_OK;
}

static int __stdcall dev_SetCursorProperties(struct IDirect3DDevice9* self, uint32_t XHotSpot, uint32_t YHotSpot, void* pCursorBitmap) {
    (void)self; (void)XHotSpot; (void)YHotSpot; (void)pCursorBitmap;
    d3d9_log("IDirect3DDevice9::SetCursorProperties");
    return D3D_OK;
}

static int __stdcall dev_SetCursorPosition(struct IDirect3DDevice9* self, int X, int Y, uint32_t Flags) {
    (void)self; (void)X; (void)Y; (void)Flags;
    d3d9_log("IDirect3DDevice9::SetCursorPosition");
    return D3D_OK;
}

static int __stdcall dev_ShowCursor(struct IDirect3DDevice9* self, int bShow) {
    (void)self; (void)bShow;
    d3d9_log("IDirect3DDevice9::ShowCursor");
    return D3D_OK;
}

static int __stdcall dev_CreateAdditionalSwapChain(struct IDirect3DDevice9* self, void* pPresentationParameters, struct IDirect3DDevice9** pSwapChain) {
    (void)self; (void)pPresentationParameters; (void)pSwapChain;
    d3d9_log("IDirect3DDevice9::CreateAdditionalSwapChain");
    return D3D_OK;
}

static int __stdcall dev_GetSwapChain(struct IDirect3DDevice9* self, uint32_t iSwapChain, struct IDirect3DDevice9** pSwapChain) {
    (void)self; (void)iSwapChain; (void)pSwapChain;
    d3d9_log("IDirect3DDevice9::GetSwapChain");
    return D3D_OK;
}

static uint32_t __stdcall dev_GetNumberOfSwapChains(struct IDirect3DDevice9* self) {
    (void)self;
    d3d9_log("IDirect3DDevice9::GetNumberOfSwapChains");
    return 1;
}

static int __stdcall dev_Reset(struct IDirect3DDevice9* self, void* pPresentationParameters) {
    (void)self; (void)pPresentationParameters;
    d3d9_log("IDirect3DDevice9::Reset");
    return D3D_OK;
}

static int __stdcall dev_Present(struct IDirect3DDevice9* self, const void* pSourceRect, const void* pDestRect, void* hDestWindowOverride, void* pDirtyRegion) {
    (void)self; (void)pSourceRect; (void)pDestRect; (void)hDestWindowOverride; (void)pDirtyRegion;
    d3d9_log("IDirect3DDevice9::Present");
    return D3D_OK;
}

static int __stdcall dev_GetBackBuffer(struct IDirect3DDevice9* self, uint32_t iSwapChain, uint32_t iBackBuffer, uint32_t Type, struct IDirect3DDevice9** ppBackBuffer) {
    (void)self; (void)iSwapChain; (void)iBackBuffer; (void)Type; (void)ppBackBuffer;
    d3d9_log("IDirect3DDevice9::GetBackBuffer");
    return D3D_OK;
}

static int __stdcall dev_GetRasterStatus(struct IDirect3DDevice9* self, uint32_t iSwapChain, void* pRasterStatus) {
    (void)self; (void)iSwapChain; (void)pRasterStatus;
    d3d9_log("IDirect3DDevice9::GetRasterStatus");
    return D3D_OK;
}

static int __stdcall dev_SetDialogBoxMode(struct IDirect3DDevice9* self, int bEnable) {
    (void)self; (void)bEnable;
    d3d9_log("IDirect3DDevice9::SetDialogBoxMode");
    return D3D_OK;
}

static int __stdcall dev_SetGammaRamp(struct IDirect3DDevice9* self, uint32_t iSwapChain, uint32_t Flags, const void* pGammaRamp) {
    (void)self; (void)iSwapChain; (void)Flags; (void)pGammaRamp;
    d3d9_log("IDirect3DDevice9::SetGammaRamp");
    return D3D_OK;
}

static int __stdcall dev_GetGammaRamp(struct IDirect3DDevice9* self, uint32_t iSwapChain, uint32_t Flags, void* pGammaRamp) {
    (void)self; (void)iSwapChain; (void)Flags; (void)pGammaRamp;
    d3d9_log("IDirect3DDevice9::GetGammaRamp");
    return D3D_OK;
}

static int __stdcall dev_CreateTexture(struct IDirect3DDevice9* self, uint32_t Width, uint32_t Height, uint32_t Levels, uint32_t Usage, uint32_t Format, uint32_t Pool, struct IDirect3DTexture9** ppTexture, void* pSharedHandle) {
    (void)self; (void)Usage; (void)Format; (void)Pool; (void)pSharedHandle;
    d3d9_log("IDirect3DDevice9::CreateTexture(%ux%u, Levels=%u, Usage=%u, Format=%u, Pool=%u)", Width, Height, Levels, Usage, Format, Pool);
    if (!ppTexture) return D3DERR_INVALIDCALL;
    
    TextureImpl* impl = calloc(1, sizeof(TextureImpl));
    if (!impl) return D3DERR_INVALIDCALL;
    
    impl->vtbl = &g_tex_vtbl;
    impl->base.refcount = 1;
    impl->width = Width;
    impl->height = Height;
    impl->levels = Levels ? Levels : 1;
    impl->usage = Usage;
    impl->format = Format;
    impl->pool = Pool;
    
    *ppTexture = (struct IDirect3DTexture9*)impl;
    return D3D_OK;
}

static int __stdcall dev_CreateVolumeTexture(struct IDirect3DDevice9* self, uint32_t Width, uint32_t Height, uint32_t Depth, uint32_t Levels, uint32_t Usage, uint32_t Format, uint32_t Pool, struct IDirect3DTexture9** ppTexture, void* pSharedHandle) {
    (void)self; (void)Width; (void)Height; (void)Depth; (void)Levels; (void)Usage; (void)Format; (void)Pool; (void)ppTexture; (void)pSharedHandle;
    d3d9_log("IDirect3DDevice9::CreateVolumeTexture");
    return D3D_OK;
}

static int __stdcall dev_CreateCubeTexture(struct IDirect3DDevice9* self, uint32_t EdgeLength, uint32_t Levels, uint32_t Usage, uint32_t Format, uint32_t Pool, struct IDirect3DTexture9** ppTexture, void* pSharedHandle) {
    (void)self; (void)EdgeLength; (void)Levels; (void)Usage; (void)Format; (void)Pool; (void)ppTexture; (void)pSharedHandle;
    d3d9_log("IDirect3DDevice9::CreateCubeTexture");
    return D3D_OK;
}

static int __stdcall dev_CreateVertexBuffer(struct IDirect3DDevice9* self, uint32_t Length, uint32_t Usage, uint32_t FVF, uint32_t Pool, struct IDirect3DVertexBuffer9** ppVertexBuffer, void* pSharedHandle) {
    (void)self; (void)Usage; (void)FVF; (void)Pool; (void)pSharedHandle;
    d3d9_log("IDirect3DDevice9::CreateVertexBuffer(Length=%u, Usage=%u, FVF=%u, Pool=%u)", Length, Usage, FVF, Pool);
    if (!ppVertexBuffer) return D3DERR_INVALIDCALL;
    
    VertexBufferImpl* impl = calloc(1, sizeof(VertexBufferImpl));
    if (!impl) return D3DERR_INVALIDCALL;
    
    impl->vtbl = &g_vb_vtbl;
    impl->base.refcount = 1;
    impl->size = Length;
    impl->fvf = FVF;
    impl->usage = Usage;
    impl->data = NULL;
    
    *ppVertexBuffer = (struct IDirect3DVertexBuffer9*)impl;
    return D3D_OK;
}

static int __stdcall dev_CreateIndexBuffer(struct IDirect3DDevice9* self, uint32_t Length, uint32_t Usage, uint32_t Format, uint32_t Pool, struct IDirect3DIndexBuffer9** ppIndexBuffer, void* pSharedHandle) {
    (void)self; (void)Usage; (void)Format; (void)Pool; (void)pSharedHandle;
    d3d9_log("IDirect3DDevice9::CreateIndexBuffer(Length=%u, Usage=%u, Format=%u, Pool=%u)", Length, Usage, Format, Pool);
    if (!ppIndexBuffer) return D3DERR_INVALIDCALL;
    
    IndexBufferImpl* impl = calloc(1, sizeof(IndexBufferImpl));
    if (!impl) return D3DERR_INVALIDCALL;
    
    impl->vtbl = &g_ib_vtbl;
    impl->base.refcount = 1;
    impl->size = Length;
    impl->format = Format;
    impl->usage = Usage;
    impl->data = NULL;
    
    *ppIndexBuffer = (struct IDirect3DIndexBuffer9*)impl;
    return D3D_OK;
}

static int __stdcall dev_CreateRenderTarget(struct IDirect3DDevice9* self, uint32_t Width, uint32_t Height, uint32_t Format, uint32_t MultiSampleType, uint32_t MultiSampleQuality, struct IDirect3DDevice9** ppSurface, void* pSharedHandle) {
    (void)self; (void)Width; (void)Height; (void)Format; (void)MultiSampleType; (void)MultiSampleQuality; (void)ppSurface; (void)pSharedHandle;
    d3d9_log("IDirect3DDevice9::CreateRenderTarget");
    return D3D_OK;
}

static int __stdcall dev_CreateDepthStencilSurface(struct IDirect3DDevice9* self, uint32_t Width, uint32_t Height, uint32_t Format, uint32_t MultiSampleType, uint32_t MultiSampleQuality, int Discard, struct IDirect3DDevice9** ppSurface, void* pSharedHandle) {
    (void)self; (void)Width; (void)Height; (void)Format; (void)MultiSampleType; (void)MultiSampleQuality; (void)Discard; (void)ppSurface; (void)pSharedHandle;
    d3d9_log("IDirect3DDevice9::CreateDepthStencilSurface");
    return D3D_OK;
}

static int __stdcall dev_UpdateSurface(struct IDirect3DDevice9* self, void* pSourceSurface, void* pSourceRect, void* pDestinationSurface, const void* pDestPoint) {
    (void)self; (void)pSourceSurface; (void)pSourceRect; (void)pDestinationSurface; (void)pDestPoint;
    d3d9_log("IDirect3DDevice9::UpdateSurface");
    return D3D_OK;
}

static int __stdcall dev_UpdateTexture(struct IDirect3DDevice9* self, void* pSourceTexture, void* pDestinationTexture) {
    (void)self; (void)pSourceTexture; (void)pDestinationTexture;
    d3d9_log("IDirect3DDevice9::UpdateTexture");
    return D3D_OK;
}

static int __stdcall dev_GetRenderTargetData(struct IDirect3DDevice9* self, void* pRenderTarget, void* pDestSurface) {
    (void)self; (void)pRenderTarget; (void)pDestSurface;
    d3d9_log("IDirect3DDevice9::GetRenderTargetData");
    return D3D_OK;
}

static int __stdcall dev_GetFrontBufferData(struct IDirect3DDevice9* self, uint32_t iSwapChain, void* pDestSurface) {
    (void)self; (void)iSwapChain; (void)pDestSurface;
    d3d9_log("IDirect3DDevice9::GetFrontBufferData");
    return D3D_OK;
}

static int __stdcall dev_StretchRect(struct IDirect3DDevice9* self, void* pSourceSurface, const void* pSourceRect, void* pDestSurface, const void* pDestRect, uint32_t Filter) {
    (void)self; (void)pSourceSurface; (void)pSourceRect; (void)pDestSurface; (void)pDestRect; (void)Filter;
    d3d9_log("IDirect3DDevice9::StretchRect");
    return D3D_OK;
}

static int __stdcall dev_ColorFill(struct IDirect3DDevice9* self, void* pSurface, const void* pRect, uint32_t Color) {
    (void)self; (void)pSurface; (void)pRect; (void)Color;
    d3d9_log("IDirect3DDevice9::ColorFill");
    return D3D_OK;
}

static int __stdcall dev_CreateOffscreenPlainSurface(struct IDirect3DDevice9* self, uint32_t Width, uint32_t Height, uint32_t Format, uint32_t Pool, struct IDirect3DDevice9** ppSurface, void* pSharedHandle) {
    (void)self; (void)Width; (void)Height; (void)Format; (void)Pool; (void)ppSurface; (void)pSharedHandle;
    d3d9_log("IDirect3DDevice9::CreateOffscreenPlainSurface");
    return D3D_OK;
}

static int __stdcall dev_SetRenderTarget(struct IDirect3DDevice9* self, uint32_t RenderTargetIndex, void* pRenderTarget) {
    (void)self; (void)RenderTargetIndex; (void)pRenderTarget;
    d3d9_log("IDirect3DDevice9::SetRenderTarget");
    return D3D_OK;
}

static int __stdcall dev_GetRenderTarget(struct IDirect3DDevice9* self, uint32_t RenderTargetIndex, void** ppRenderTarget) {
    (void)self; (void)RenderTargetIndex; (void)ppRenderTarget;
    d3d9_log("IDirect3DDevice9::GetRenderTarget");
    return D3D_OK;
}

static int __stdcall dev_SetDepthStencilSurface(struct IDirect3DDevice9* self, void* pNewZStencil) {
    (void)self; (void)pNewZStencil;
    d3d9_log("IDirect3DDevice9::SetDepthStencilSurface");
    return D3D_OK;
}

static int __stdcall dev_GetDepthStencilSurface(struct IDirect3DDevice9* self, void** ppZStencilSurface) {
    (void)self; (void)ppZStencilSurface;
    d3d9_log("IDirect3DDevice9::GetDepthStencilSurface");
    return D3D_OK;
}

static int __stdcall dev_BeginScene(struct IDirect3DDevice9* self) {
    (void)self;
    d3d9_log("IDirect3DDevice9::BeginScene");
    return D3D_OK;
}

static int __stdcall dev_EndScene(struct IDirect3DDevice9* self) {
    (void)self;
    d3d9_log("IDirect3DDevice9::EndScene");
    return D3D_OK;
}

static int __stdcall dev_Clear(struct IDirect3DDevice9* self, uint32_t Count, const void* pRects, uint32_t Flags, uint32_t Color, float Z, uint32_t Stencil) {
    (void)self; (void)pRects;
    d3d9_log("IDirect3DDevice9::Clear(Count=%u, Flags=0x%x, Color=0x%x, Z=%f, Stencil=%u)", Count, Flags, Color, Z, Stencil);
    return D3D_OK;
}

static int __stdcall dev_SetTransform(struct IDirect3DDevice9* self, uint32_t State, const void* pMatrix) {
    DeviceImpl* impl = (DeviceImpl*)self;
    d3d9_log("IDirect3DDevice9::SetTransform(State=%u)", State);
    if (State < 256 && pMatrix) {
        impl->transforms[State] = *(const D3DXMATRIX*)pMatrix;
    }
    return D3D_OK;
}

static int __stdcall dev_GetTransform(struct IDirect3DDevice9* self, uint32_t State, void* pMatrix) {
    DeviceImpl* impl = (DeviceImpl*)self;
    d3d9_log("IDirect3DDevice9::GetTransform(State=%u)", State);
    if (State < 256 && pMatrix) {
        *(D3DXMATRIX*)pMatrix = impl->transforms[State];
    }
    return D3D_OK;
}

static int __stdcall dev_MultiplyTransform(struct IDirect3DDevice9* self, uint32_t State, const void* pMatrix) {
    (void)self; (void)State; (void)pMatrix;
    d3d9_log("IDirect3DDevice9::MultiplyTransform");
    return D3D_OK;
}

static int __stdcall dev_SetViewport(struct IDirect3DDevice9* self, const void* pViewport) {
    DeviceImpl* impl = (DeviceImpl*)self;
    d3d9_log("IDirect3DDevice9::SetViewport");
    if (pViewport) {
        impl->viewport = *(const D3DVIEWPORT9*)pViewport;
    }
    return D3D_OK;
}

static int __stdcall dev_GetViewport(struct IDirect3DDevice9* self, void* pViewport) {
    DeviceImpl* impl = (DeviceImpl*)self;
    d3d9_log("IDirect3DDevice9::GetViewport");
    if (pViewport) {
        *(D3DVIEWPORT9*)pViewport = impl->viewport;
    }
    return D3D_OK;
}

static int __stdcall dev_SetMaterial(struct IDirect3DDevice9* self, const void* pMaterial) {
    (void)self; (void)pMaterial;
    d3d9_log("IDirect3DDevice9::SetMaterial");
    return D3D_OK;
}

static int __stdcall dev_GetMaterial(struct IDirect3DDevice9* self, void* pMaterial) {
    (void)self; (void)pMaterial;
    d3d9_log("IDirect3DDevice9::GetMaterial");
    return D3D_OK;
}

static int __stdcall dev_SetLight(struct IDirect3DDevice9* self, uint32_t Index, const void* pLight) {
    (void)self; (void)Index; (void)pLight;
    d3d9_log("IDirect3DDevice9::SetLight");
    return D3D_OK;
}

static int __stdcall dev_GetLight(struct IDirect3DDevice9* self, uint32_t Index, void* pLight) {
    (void)self; (void)Index; (void)pLight;
    d3d9_log("IDirect3DDevice9::GetLight");
    return D3D_OK;
}

static int __stdcall dev_LightEnable(struct IDirect3DDevice9* self, uint32_t Index, int Enable) {
    (void)self; (void)Index; (void)Enable;
    d3d9_log("IDirect3DDevice9::LightEnable");
    return D3D_OK;
}

static int __stdcall dev_GetLightEnable(struct IDirect3DDevice9* self, uint32_t Index, int* pEnable) {
    (void)self; (void)Index; (void)pEnable;
    d3d9_log("IDirect3DDevice9::GetLightEnable");
    return D3D_OK;
}

static int __stdcall dev_SetClipPlane(struct IDirect3DDevice9* self, uint32_t Index, const float* pPlane) {
    (void)self; (void)Index; (void)pPlane;
    d3d9_log("IDirect3DDevice9::SetClipPlane");
    return D3D_OK;
}

static int __stdcall dev_GetClipPlane(struct IDirect3DDevice9* self, uint32_t Index, float* pPlane) {
    (void)self; (void)Index; (void)pPlane;
    d3d9_log("IDirect3DDevice9::GetClipPlane");
    return D3D_OK;
}

static int __stdcall dev_SetRenderState(struct IDirect3DDevice9* self, uint32_t State, uint32_t Value) {
    DeviceImpl* impl = (DeviceImpl*)self;
    d3d9_log("IDirect3DDevice9::SetRenderState(State=%u, Value=0x%x)", State, Value);
    if (State < 256) {
        impl->render_state[State] = Value;
    }
    return D3D_OK;
}

static int __stdcall dev_GetRenderState(struct IDirect3DDevice9* self, uint32_t State, uint32_t* pValue) {
    DeviceImpl* impl = (DeviceImpl*)self;
    d3d9_log("IDirect3DDevice9::GetRenderState(State=%u)", State);
    if (State < 256 && pValue) {
        *pValue = impl->render_state[State];
    }
    return D3D_OK;
}

static int __stdcall dev_CreateStateBlock(struct IDirect3DDevice9* self, uint32_t Type, struct IDirect3DDevice9** ppSB) {
    (void)self; (void)Type; (void)ppSB;
    d3d9_log("IDirect3DDevice9::CreateStateBlock");
    return D3D_OK;
}

static int __stdcall dev_BeginStateBlock(struct IDirect3DDevice9* self) {
    (void)self;
    d3d9_log("IDirect3DDevice9::BeginStateBlock");
    return D3D_OK;
}

static int __stdcall dev_EndStateBlock(struct IDirect3DDevice9* self, struct IDirect3DDevice9** ppSB) {
    (void)self; (void)ppSB;
    d3d9_log("IDirect3DDevice9::EndStateBlock");
    return D3D_OK;
}

static int __stdcall dev_SetClipStatus(struct IDirect3DDevice9* self, const void* pClipStatus) {
    (void)self; (void)pClipStatus;
    d3d9_log("IDirect3DDevice9::SetClipStatus");
    return D3D_OK;
}

static int __stdcall dev_GetClipStatus(struct IDirect3DDevice9* self, void* pClipStatus) {
    (void)self; (void)pClipStatus;
    d3d9_log("IDirect3DDevice9::GetClipStatus");
    return D3D_OK;
}

static int __stdcall dev_GetTexture(struct IDirect3DDevice9* self, uint32_t Stage, void** ppTexture) {
    DeviceImpl* impl = (DeviceImpl*)self;
    d3d9_log("IDirect3DDevice9::GetTexture(Stage=%u)", Stage);
    if (Stage < 8 && ppTexture) {
        *ppTexture = impl->textures[Stage];
    }
    return D3D_OK;
}

static int __stdcall dev_SetTexture(struct IDirect3DDevice9* self, uint32_t Stage, void* pTexture) {
    DeviceImpl* impl = (DeviceImpl*)self;
    d3d9_log("IDirect3DDevice9::SetTexture(Stage=%u, pTexture=%p)", Stage, pTexture);
    if (Stage < 8) {
        impl->textures[Stage] = (struct IDirect3DTexture9*)pTexture;
    }
    return D3D_OK;
}

static int __stdcall dev_GetTextureStageState(struct IDirect3DDevice9* self, uint32_t Stage, uint32_t Type, uint32_t* pValue) {
    (void)self; (void)Stage; (void)Type; (void)pValue;
    d3d9_log("IDirect3DDevice9::GetTextureStageState");
    return D3D_OK;
}

static int __stdcall dev_SetTextureStageState(struct IDirect3DDevice9* self, uint32_t Stage, uint32_t Type, uint32_t Value) {
    (void)self; (void)Stage; (void)Type; (void)Value;
    d3d9_log("IDirect3DDevice9::SetTextureStageState");
    return D3D_OK;
}

static int __stdcall dev_GetSamplerState(struct IDirect3DDevice9* self, uint32_t Stage, uint32_t Type, uint32_t* pValue) {
    (void)self; (void)Stage; (void)Type; (void)pValue;
    d3d9_log("IDirect3DDevice9::GetSamplerState");
    return D3D_OK;
}

static int __stdcall dev_SetSamplerState(struct IDirect3DDevice9* self, uint32_t Stage, uint32_t Type, uint32_t Value) {
    (void)self; (void)Stage; (void)Type; (void)Value;
    d3d9_log("IDirect3DDevice9::SetSamplerState");
    return D3D_OK;
}

static int __stdcall dev_ValidateDevice(struct IDirect3DDevice9* self, uint32_t* pNumPasses) {
    (void)self; (void)pNumPasses;
    d3d9_log("IDirect3DDevice9::ValidateDevice");
    return D3D_OK;
}

static int __stdcall dev_SetPaletteEntries(struct IDirect3DDevice9* self, uint32_t PaletteNumber, const void* pEntries) {
    (void)self; (void)PaletteNumber; (void)pEntries;
    d3d9_log("IDirect3DDevice9::SetPaletteEntries");
    return D3D_OK;
}

static int __stdcall dev_GetPaletteEntries(struct IDirect3DDevice9* self, uint32_t PaletteNumber, void* pEntries) {
    (void)self; (void)PaletteNumber; (void)pEntries;
    d3d9_log("IDirect3DDevice9::GetPaletteEntries");
    return D3D_OK;
}

static int __stdcall dev_SetCurrentTexturePalette(struct IDirect3DDevice9* self, uint32_t PaletteNumber) {
    (void)self; (void)PaletteNumber;
    d3d9_log("IDirect3DDevice9::SetCurrentTexturePalette");
    return D3D_OK;
}

static int __stdcall dev_GetCurrentTexturePalette(struct IDirect3DDevice9* self, uint32_t* pPaletteNumber) {
    (void)self; (void)pPaletteNumber;
    d3d9_log("IDirect3DDevice9::GetCurrentTexturePalette");
    return D3D_OK;
}

static int __stdcall dev_SetScissorRect(struct IDirect3DDevice9* self, const void* pRect) {
    (void)self; (void)pRect;
    d3d9_log("IDirect3DDevice9::SetScissorRect");
    return D3D_OK;
}

static int __stdcall dev_GetScissorRect(struct IDirect3DDevice9* self, void* pRect) {
    (void)self; (void)pRect;
    d3d9_log("IDirect3DDevice9::GetScissorRect");
    return D3D_OK;
}

static int __stdcall dev_SetSoftwareVertexProcessing(struct IDirect3DDevice9* self, int bSoftware) {
    (void)self; (void)bSoftware;
    d3d9_log("IDirect3DDevice9::SetSoftwareVertexProcessing");
    return D3D_OK;
}

static int __stdcall dev_GetSoftwareVertexProcessing(struct IDirect3DDevice9* self, int* pbSoftware) {
    (void)self; (void)pbSoftware;
    d3d9_log("IDirect3DDevice9::GetSoftwareVertexProcessing");
    return D3D_OK;
}

static int __stdcall dev_SetNPatchMode(struct IDirect3DDevice9* self, float nSegments) {
    (void)self; (void)nSegments;
    d3d9_log("IDirect3DDevice9::SetNPatchMode");
    return D3D_OK;
}

static int __stdcall dev_GetNPatchMode(struct IDirect3DDevice9* self, float* pnSegments) {
    (void)self; (void)pnSegments;
    d3d9_log("IDirect3DDevice9::GetNPatchMode");
    return D3D_OK;
}

static int __stdcall dev_DrawPrimitive(struct IDirect3DDevice9* self, uint32_t PrimitiveType, uint32_t StartVertex, uint32_t PrimitiveCount) {
    (void)self;
    d3d9_log("IDirect3DDevice9::DrawPrimitive(Type=%u, Start=%u, Count=%u)", PrimitiveType, StartVertex, PrimitiveCount);
    return D3D_OK;
}

static int __stdcall dev_DrawIndexedPrimitive(struct IDirect3DDevice9* self, uint32_t PrimitiveType, int BaseVertexIndex, uint32_t MinVertexIndex, uint32_t NumVertices, uint32_t StartIndex, uint32_t PrimitiveCount) {
    (void)self; (void)PrimitiveType; (void)BaseVertexIndex; (void)MinVertexIndex; (void)NumVertices; (void)StartIndex; (void)PrimitiveCount;
    d3d9_log("IDirect3DDevice9::DrawIndexedPrimitive");
    return D3D_OK;
}

static int __stdcall dev_DrawPrimitiveUP(struct IDirect3DDevice9* self, uint32_t PrimitiveType, uint32_t PrimitiveCount, const void* pVertexStreamZeroData, uint32_t VertexStreamZeroStride) {
    (void)self; (void)pVertexStreamZeroData;
    d3d9_log("IDirect3DDevice9::DrawPrimitiveUP(Type=%u, Count=%u, Stride=%u)", PrimitiveType, PrimitiveCount, VertexStreamZeroStride);
    return D3D_OK;
}

static int __stdcall dev_DrawIndexedPrimitiveUP(struct IDirect3DDevice9* self, uint32_t PrimitiveType, int BaseVertexIndex, uint32_t MinVertexIndex, uint32_t NumVertices, uint32_t PrimitiveCount, const void* pIndexData, uint32_t IndexDataFormat, const void* pVertexStreamZeroData, uint32_t VertexStreamZeroStride) {
    (void)self; (void)PrimitiveType; (void)BaseVertexIndex; (void)MinVertexIndex; (void)NumVertices; (void)PrimitiveCount; (void)pIndexData; (void)IndexDataFormat; (void)pVertexStreamZeroData; (void)VertexStreamZeroStride;
    d3d9_log("IDirect3DDevice9::DrawIndexedPrimitiveUP");
    return D3D_OK;
}

static int __stdcall dev_ProcessVertices(struct IDirect3DDevice9* self, uint32_t SrcStartIndex, uint32_t DestIndex, uint32_t VertexCount, void* pDestBuffer, void* pVertexDecl, uint32_t Flags) {
    (void)self; (void)SrcStartIndex; (void)DestIndex; (void)VertexCount; (void)pDestBuffer; (void)pVertexDecl; (void)Flags;
    d3d9_log("IDirect3DDevice9::ProcessVertices");
    return D3D_OK;
}

static int __stdcall dev_CreateVertexDeclaration(struct IDirect3DDevice9* self, const void* pVertexElements, void** ppDecl) {
    (void)self; (void)pVertexElements; (void)ppDecl;
    d3d9_log("IDirect3DDevice9::CreateVertexDeclaration");
    return D3D_OK;
}

static int __stdcall dev_SetVertexDeclaration(struct IDirect3DDevice9* self, void* pDecl) {
    (void)self; (void)pDecl;
    d3d9_log("IDirect3DDevice9::SetVertexDeclaration");
    return D3D_OK;
}

static int __stdcall dev_GetVertexDeclaration(struct IDirect3DDevice9* self, void** ppDecl) {
    (void)self; (void)ppDecl;
    d3d9_log("IDirect3DDevice9::GetVertexDeclaration");
    return D3D_OK;
}

static int __stdcall dev_SetFVF(struct IDirect3DDevice9* self, uint32_t FVF) {
    DeviceImpl* impl = (DeviceImpl*)self;
    d3d9_log("IDirect3DDevice9::SetFVF(FVF=0x%x)", FVF);
    impl->fvf = FVF;
    return D3D_OK;
}

static int __stdcall dev_GetFVF(struct IDirect3DDevice9* self, uint32_t* pFVF) {
    DeviceImpl* impl = (DeviceImpl*)self;
    d3d9_log("IDirect3DDevice9::GetFVF");
    if (pFVF) *pFVF = impl->fvf;
    return D3D_OK;
}

static int __stdcall dev_CreateVertexShader(struct IDirect3DDevice9* self, const void* pFunction, void** ppShader) {
    (void)self;
    d3d9_log("IDirect3DDevice9::CreateVertexShader");
    return shader_create(pFunction, ppShader, 1);
}

static int __stdcall dev_SetVertexShader(struct IDirect3DDevice9* self, void* pShader) {
    DeviceImpl* impl = (DeviceImpl*)self;
    d3d9_log("IDirect3DDevice9::SetVertexShader(%p)", pShader);
    impl->bound_vs = pShader;
    return D3D_OK;
}

static int __stdcall dev_GetVertexShader(struct IDirect3DDevice9* self, void** ppShader) {
    DeviceImpl* impl = (DeviceImpl*)self;
    d3d9_log("IDirect3DDevice9::GetVertexShader");
    if (ppShader) *ppShader = impl->bound_vs;
    return D3D_OK;
}

static int __stdcall dev_SetVertexShaderConstantF(struct IDirect3DDevice9* self, uint32_t StartRegister, const float* pConstantData, uint32_t Vector4fCount) {
    (void)self; (void)StartRegister; (void)pConstantData; (void)Vector4fCount;
    d3d9_log("IDirect3DDevice9::SetVertexShaderConstantF");
    return D3D_OK;
}

static int __stdcall dev_GetVertexShaderConstantF(struct IDirect3DDevice9* self, uint32_t StartRegister, float* pConstantData, uint32_t Vector4fCount) {
    (void)self; (void)StartRegister; (void)pConstantData; (void)Vector4fCount;
    d3d9_log("IDirect3DDevice9::GetVertexShaderConstantF");
    return D3D_OK;
}

static int __stdcall dev_SetVertexShaderConstantI(struct IDirect3DDevice9* self, uint32_t StartRegister, const int* pConstantData, uint32_t Vector4iCount) {
    (void)self; (void)StartRegister; (void)pConstantData; (void)Vector4iCount;
    d3d9_log("IDirect3DDevice9::SetVertexShaderConstantI");
    return D3D_OK;
}

static int __stdcall dev_GetVertexShaderConstantI(struct IDirect3DDevice9* self, uint32_t StartRegister, int* pConstantData, uint32_t Vector4iCount) {
    (void)self; (void)StartRegister; (void)pConstantData; (void)Vector4iCount;
    d3d9_log("IDirect3DDevice9::GetVertexShaderConstantI");
    return D3D_OK;
}

static int __stdcall dev_SetVertexShaderConstantB(struct IDirect3DDevice9* self, uint32_t StartRegister, const int* pConstantData, uint32_t BoolCount) {
    (void)self; (void)StartRegister; (void)pConstantData; (void)BoolCount;
    d3d9_log("IDirect3DDevice9::SetVertexShaderConstantB");
    return D3D_OK;
}

static int __stdcall dev_GetVertexShaderConstantB(struct IDirect3DDevice9* self, uint32_t StartRegister, int* pConstantData, uint32_t BoolCount) {
    (void)self; (void)StartRegister; (void)pConstantData; (void)BoolCount;
    d3d9_log("IDirect3DDevice9::GetVertexShaderConstantB");
    return D3D_OK;
}

static int __stdcall dev_SetStreamSource(struct IDirect3DDevice9* self, uint32_t StreamNumber, void* pStreamData, uint32_t OffsetInBytes, uint32_t Stride) {
    DeviceImpl* impl = (DeviceImpl*)self;
    d3d9_log("IDirect3DDevice9::SetStreamSource(Stream=%u, pData=%p, Offset=%u, Stride=%u)", StreamNumber, pStreamData, OffsetInBytes, Stride);
    if (StreamNumber < 8) {
        impl->stream_source[StreamNumber] = (struct IDirect3DVertexBuffer9*)pStreamData;
    }
    return D3D_OK;
}

static int __stdcall dev_GetStreamSource(struct IDirect3DDevice9* self, uint32_t StreamNumber, void** ppStreamData, uint32_t* pOffsetInBytes, uint32_t* pStride) {
    (void)self; (void)StreamNumber; (void)ppStreamData; (void)pOffsetInBytes; (void)pStride;
    d3d9_log("IDirect3DDevice9::GetStreamSource");
    return D3D_OK;
}

static int __stdcall dev_SetStreamSourceFreq(struct IDirect3DDevice9* self, uint32_t StreamNumber, uint32_t Setting) {
    (void)self; (void)StreamNumber; (void)Setting;
    d3d9_log("IDirect3DDevice9::SetStreamSourceFreq");
    return D3D_OK;
}

static int __stdcall dev_GetStreamSourceFreq(struct IDirect3DDevice9* self, uint32_t StreamNumber, uint32_t* pSetting) {
    (void)self; (void)StreamNumber; (void)pSetting;
    d3d9_log("IDirect3DDevice9::GetStreamSourceFreq");
    return D3D_OK;
}

static int __stdcall dev_SetIndices(struct IDirect3DDevice9* self, void* pIndexData) {
    DeviceImpl* impl = (DeviceImpl*)self;
    d3d9_log("IDirect3DDevice9::SetIndices(pIndexData=%p)", pIndexData);
    impl->index_buffer = (struct IDirect3DIndexBuffer9*)pIndexData;
    return D3D_OK;
}

static int __stdcall dev_GetIndices(struct IDirect3DDevice9* self, void** ppIndexData) {
    DeviceImpl* impl = (DeviceImpl*)self;
    d3d9_log("IDirect3DDevice9::GetIndices");
    if (ppIndexData) *ppIndexData = impl->index_buffer;
    return D3D_OK;
}

static int __stdcall dev_CreatePixelShader(struct IDirect3DDevice9* self, const void* pFunction, void** ppShader) {
    (void)self;
    d3d9_log("IDirect3DDevice9::CreatePixelShader");
    return shader_create(pFunction, ppShader, 0);
}

static int __stdcall dev_SetPixelShader(struct IDirect3DDevice9* self, void* pShader) {
    DeviceImpl* impl = (DeviceImpl*)self;
    d3d9_log("IDirect3DDevice9::SetPixelShader(%p)", pShader);
    impl->bound_ps = pShader;
    return D3D_OK;
}

static int __stdcall dev_GetPixelShader(struct IDirect3DDevice9* self, void** ppShader) {
    DeviceImpl* impl = (DeviceImpl*)self;
    d3d9_log("IDirect3DDevice9::GetPixelShader");
    if (ppShader) *ppShader = impl->bound_ps;
    return D3D_OK;
}

static int __stdcall dev_SetPixelShaderConstantF(struct IDirect3DDevice9* self, uint32_t StartRegister, const float* pConstantData, uint32_t Vector4fCount) {
    (void)self; (void)StartRegister; (void)pConstantData; (void)Vector4fCount;
    d3d9_log("IDirect3DDevice9::SetPixelShaderConstantF");
    return D3D_OK;
}

static int __stdcall dev_GetPixelShaderConstantF(struct IDirect3DDevice9* self, uint32_t StartRegister, float* pConstantData, uint32_t Vector4fCount) {
    (void)self; (void)StartRegister; (void)pConstantData; (void)Vector4fCount;
    d3d9_log("IDirect3DDevice9::GetPixelShaderConstantF");
    return D3D_OK;
}

static int __stdcall dev_SetPixelShaderConstantI(struct IDirect3DDevice9* self, uint32_t StartRegister, const int* pConstantData, uint32_t Vector4iCount) {
    (void)self; (void)StartRegister; (void)pConstantData; (void)Vector4iCount;
    d3d9_log("IDirect3DDevice9::SetPixelShaderConstantI");
    return D3D_OK;
}

static int __stdcall dev_GetPixelShaderConstantI(struct IDirect3DDevice9* self, uint32_t StartRegister, int* pConstantData, uint32_t Vector4iCount) {
    (void)self; (void)StartRegister; (void)pConstantData; (void)Vector4iCount;
    d3d9_log("IDirect3DDevice9::GetPixelShaderConstantI");
    return D3D_OK;
}

static int __stdcall dev_SetPixelShaderConstantB(struct IDirect3DDevice9* self, uint32_t StartRegister, const int* pConstantData, uint32_t BoolCount) {
    (void)self; (void)StartRegister; (void)pConstantData; (void)BoolCount;
    d3d9_log("IDirect3DDevice9::SetPixelShaderConstantB");
    return D3D_OK;
}

static int __stdcall dev_GetPixelShaderConstantB(struct IDirect3DDevice9* self, uint32_t StartRegister, int* pConstantData, uint32_t BoolCount) {
    (void)self; (void)StartRegister; (void)pConstantData; (void)BoolCount;
    d3d9_log("IDirect3DDevice9::GetPixelShaderConstantB");
    return D3D_OK;
}

static int __stdcall dev_DrawRectPatch(struct IDirect3DDevice9* self, uint32_t Handle, const float* pNumSegs, void* pRectPatchInfo) {
    (void)self; (void)Handle; (void)pNumSegs; (void)pRectPatchInfo;
    d3d9_log("IDirect3DDevice9::DrawRectPatch");
    return D3D_OK;
}

static int __stdcall dev_DrawTriPatch(struct IDirect3DDevice9* self, uint32_t Handle, const float* pNumSegs, void* pTriPatchInfo) {
    (void)self; (void)Handle; (void)pNumSegs; (void)pTriPatchInfo;
    d3d9_log("IDirect3DDevice9::DrawTriPatch");
    return D3D_OK;
}

static int __stdcall dev_DeletePatch(struct IDirect3DDevice9* self, uint32_t Handle) {
    (void)self; (void)Handle;
    d3d9_log("IDirect3DDevice9::DeletePatch");
    return D3D_OK;
}

static int __stdcall dev_CreateQuery(struct IDirect3DDevice9* self, uint32_t Type, void** ppQuery) {
    (void)self; (void)Type; (void)ppQuery;
    d3d9_log("IDirect3DDevice9::CreateQuery");
    return D3D_OK;
}

static const IDirect3DDevice9Vtbl g_device_vtbl = {
    .QueryInterface = dev_QueryInterface,
    .AddRef = dev_AddRef,
    .Release = dev_Release,
    .TestCooperativeLevel = dev_TestCooperativeLevel,
    .GetAvailableTextureMem = dev_GetAvailableTextureMem,
    .EvictManagedResources = dev_EvictManagedResources,
    .GetDirect3D = dev_GetDirect3D,
    .GetDeviceCaps = dev_GetDeviceCaps,
    .GetDisplayMode = dev_GetDisplayMode,
    .GetCreationParameters = dev_GetCreationParameters,
    .SetCursorProperties = dev_SetCursorProperties,
    .SetCursorPosition = dev_SetCursorPosition,
    .ShowCursor = dev_ShowCursor,
    .CreateAdditionalSwapChain = dev_CreateAdditionalSwapChain,
    .GetSwapChain = dev_GetSwapChain,
    .GetNumberOfSwapChains = dev_GetNumberOfSwapChains,
    .Reset = dev_Reset,
    .Present = dev_Present,
    .GetBackBuffer = dev_GetBackBuffer,
    .GetRasterStatus = dev_GetRasterStatus,
    .SetDialogBoxMode = dev_SetDialogBoxMode,
    .SetGammaRamp = dev_SetGammaRamp,
    .GetGammaRamp = dev_GetGammaRamp,
    .CreateTexture = dev_CreateTexture,
    .CreateVolumeTexture = dev_CreateVolumeTexture,
    .CreateCubeTexture = dev_CreateCubeTexture,
    .CreateVertexBuffer = dev_CreateVertexBuffer,
    .CreateIndexBuffer = dev_CreateIndexBuffer,
    .CreateRenderTarget = dev_CreateRenderTarget,
    .CreateDepthStencilSurface = dev_CreateDepthStencilSurface,
    .UpdateSurface = dev_UpdateSurface,
    .UpdateTexture = dev_UpdateTexture,
    .GetRenderTargetData = dev_GetRenderTargetData,
    .GetFrontBufferData = dev_GetFrontBufferData,
    .StretchRect = dev_StretchRect,
    .ColorFill = dev_ColorFill,
    .CreateOffscreenPlainSurface = dev_CreateOffscreenPlainSurface,
    .SetRenderTarget = dev_SetRenderTarget,
    .GetRenderTarget = dev_GetRenderTarget,
    .SetDepthStencilSurface = dev_SetDepthStencilSurface,
    .GetDepthStencilSurface = dev_GetDepthStencilSurface,
    .BeginScene = dev_BeginScene,
    .EndScene = dev_EndScene,
    .Clear = dev_Clear,
    .SetTransform = dev_SetTransform,
    .GetTransform = dev_GetTransform,
    .MultiplyTransform = dev_MultiplyTransform,
    .SetViewport = dev_SetViewport,
    .GetViewport = dev_GetViewport,
    .SetMaterial = dev_SetMaterial,
    .GetMaterial = dev_GetMaterial,
    .SetLight = dev_SetLight,
    .GetLight = dev_GetLight,
    .LightEnable = dev_LightEnable,
    .GetLightEnable = dev_GetLightEnable,
    .SetClipPlane = dev_SetClipPlane,
    .GetClipPlane = dev_GetClipPlane,
    .SetRenderState = dev_SetRenderState,
    .GetRenderState = dev_GetRenderState,
    .CreateStateBlock = dev_CreateStateBlock,
    .BeginStateBlock = dev_BeginStateBlock,
    .EndStateBlock = dev_EndStateBlock,
    .SetClipStatus = dev_SetClipStatus,
    .GetClipStatus = dev_GetClipStatus,
    .GetTexture = dev_GetTexture,
    .SetTexture = dev_SetTexture,
    .GetTextureStageState = dev_GetTextureStageState,
    .SetTextureStageState = dev_SetTextureStageState,
    .GetSamplerState = dev_GetSamplerState,
    .SetSamplerState = dev_SetSamplerState,
    .ValidateDevice = dev_ValidateDevice,
    .SetPaletteEntries = dev_SetPaletteEntries,
    .GetPaletteEntries = dev_GetPaletteEntries,
    .SetCurrentTexturePalette = dev_SetCurrentTexturePalette,
    .GetCurrentTexturePalette = dev_GetCurrentTexturePalette,
    .SetScissorRect = dev_SetScissorRect,
    .GetScissorRect = dev_GetScissorRect,
    .SetSoftwareVertexProcessing = dev_SetSoftwareVertexProcessing,
    .GetSoftwareVertexProcessing = dev_GetSoftwareVertexProcessing,
    .SetNPatchMode = dev_SetNPatchMode,
    .GetNPatchMode = dev_GetNPatchMode,
    .DrawPrimitive = dev_DrawPrimitive,
    .DrawIndexedPrimitive = dev_DrawIndexedPrimitive,
    .DrawPrimitiveUP = dev_DrawPrimitiveUP,
    .DrawIndexedPrimitiveUP = dev_DrawIndexedPrimitiveUP,
    .ProcessVertices = dev_ProcessVertices,
    .CreateVertexDeclaration = dev_CreateVertexDeclaration,
    .SetVertexDeclaration = dev_SetVertexDeclaration,
    .GetVertexDeclaration = dev_GetVertexDeclaration,
    .SetFVF = dev_SetFVF,
    .GetFVF = dev_GetFVF,
    .CreateVertexShader = dev_CreateVertexShader,
    .SetVertexShader = dev_SetVertexShader,
    .GetVertexShader = dev_GetVertexShader,
    .SetVertexShaderConstantF = dev_SetVertexShaderConstantF,
    .GetVertexShaderConstantF = dev_GetVertexShaderConstantF,
    .SetVertexShaderConstantI = dev_SetVertexShaderConstantI,
    .GetVertexShaderConstantI = dev_GetVertexShaderConstantI,
    .SetVertexShaderConstantB = dev_SetVertexShaderConstantB,
    .GetVertexShaderConstantB = dev_GetVertexShaderConstantB,
    .SetStreamSource = dev_SetStreamSource,
    .GetStreamSource = dev_GetStreamSource,
    .SetStreamSourceFreq = dev_SetStreamSourceFreq,
    .GetStreamSourceFreq = dev_GetStreamSourceFreq,
    .SetIndices = dev_SetIndices,
    .GetIndices = dev_GetIndices,
    .CreatePixelShader = dev_CreatePixelShader,
    .SetPixelShader = dev_SetPixelShader,
    .GetPixelShader = dev_GetPixelShader,
    .SetPixelShaderConstantF = dev_SetPixelShaderConstantF,
    .GetPixelShaderConstantF = dev_GetPixelShaderConstantF,
    .SetPixelShaderConstantI = dev_SetPixelShaderConstantI,
    .GetPixelShaderConstantI = dev_GetPixelShaderConstantI,
    .SetPixelShaderConstantB = dev_SetPixelShaderConstantB,
    .GetPixelShaderConstantB = dev_GetPixelShaderConstantB,
    .DrawRectPatch = dev_DrawRectPatch,
    .DrawTriPatch = dev_DrawTriPatch,
    .DeletePatch = dev_DeletePatch,
    .CreateQuery = dev_CreateQuery,
};

static int __stdcall d3d9_CreateDevice(struct IDirect3D9* self, uint32_t Adapter, uint32_t DeviceType, void* hFocusWindow, uint32_t BehaviorFlags, void* pPresentationParameters, struct IDirect3DDevice9** ppReturnedDeviceInterface) {
    d3d9_log("IDirect3D9::CreateDevice(Adapter=%u, DeviceType=%u, hFocusWindow=%p, BehaviorFlags=0x%x)", Adapter, DeviceType, hFocusWindow, BehaviorFlags);
    
    DeviceImpl* impl = calloc(1, sizeof(DeviceImpl));
    if (!impl) return D3DERR_INVALIDCALL;
    
    impl->vtbl = &g_device_vtbl;
    impl->base.refcount = 1;
    impl->parent_d3d9 = self;
    
    if (pPresentationParameters) {
        impl->present_params = *(D3DPRESENT_PARAMETERS*)pPresentationParameters;
    }
    
    /* Initialize default render states */
    impl->render_state[D3DRS_ZENABLE] = D3DZB_TRUE;
    impl->render_state[D3DRS_CULLMODE] = D3DCULL_CCW;
    impl->render_state[D3DRS_LIGHTING] = 0;
    impl->render_state[D3DRS_ALPHABLENDENABLE] = 0;
    impl->render_state[D3DRS_SRCBLEND] = D3DBLEND_SRCALPHA;
    impl->render_state[D3DRS_DESTBLEND] = D3DBLEND_INVSRCALPHA;
    
    /* Initialize viewport from present params */
    impl->viewport.X = 0;
    impl->viewport.Y = 0;
    impl->viewport.Width = impl->present_params.BackBufferWidth ? impl->present_params.BackBufferWidth : 1280;
    impl->viewport.Height = impl->present_params.BackBufferHeight ? impl->present_params.BackBufferHeight : 720;
    impl->viewport.MinZ = 0.0f;
    impl->viewport.MaxZ = 1.0f;
    
    /* AddRef the parent D3D9 */
    if (self) self->lpVtbl->AddRef(self);
    
    *ppReturnedDeviceInterface = (struct IDirect3DDevice9*)impl;
    return D3D_OK;
}
/* COM bridge for D3D9 / DirectInput8
 * Provides guest COM objects and dispatches their vtable calls.
 */
#include "rt.h"
#include "cpu.h"
#include "d3d9_tables.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include <sys/stat.h>

#define COM_MAGIC 0xE0000000u
#define COM_BASE  0x4C000000u
#define COM_MAX   0x50000000u

#define NEL(a) ((unsigned)(sizeof(a)/sizeof(*(a))))

typedef struct { const char *name; const Method *m; unsigned n; } Iface;
static const Iface g_if[] = {
    {"?",              0,            0},
    {"IDirect3D9",       M_D3D9,       NEL(M_D3D9)},
    {"IDirect3DDevice9", M_DEVICE,     NEL(M_DEVICE)},
    {"IDirect3DTexture9",M_TEXTURE,    NEL(M_TEXTURE)},
    {"IDirect3DCubeTexture9", M_CUBETEX, NEL(M_CUBETEX)},
    {"IDirect3DVertexBuffer9", M_VBUF, NEL(M_VBUF)},
    {"IDirect3DIndexBuffer9",  M_IBUF, NEL(M_IBUF)},
    {"IDirect3DSurface9",      M_SURFACE, NEL(M_SURFACE)},
    {"IDirect3DSimple",        M_SIMPLE,  NEL(M_SIMPLE)},
    {"IDirect3DVertexShader9",   M_VSHADER,    NEL(M_VSHADER)},
    {"IDirect3DVertexDeclaration9", M_VDECL,   NEL(M_VDECL)},
    {"IDirect3DStateBlock9",     M_STATEBLOCK, NEL(M_STATEBLOCK)},
    {"IDirect3DSwapChain9",      M_SWAPCHAIN,  NEL(M_SWAPCHAIN)},
    {"IDirect3DQuery9",          M_QUERY,      NEL(M_QUERY)},
    {"IDirectInput8",            M_DI8,        NEL(M_DI8)},
    {"IDirectInputDevice8",      M_DIDEV,      NEL(M_DIDEV)},
    {"IDirectSound",             M_DS,         NEL(M_DS)},
    {"IDirectSoundBuffer",       M_DSBUF,      NEL(M_DSBUF)},
    {"IDirectInputEffect",       M_DIEFF,      NEL(M_DIEFF)},
    {"ID3DXEffect",              M_EFFECT,     NEL(M_EFFECT)},
};

enum {
    IF_D3D9 = 1, IF_DEV, IF_TEX, IF_CUBETEX, IF_VB, IF_IB, IF_SURF, IF_SIMPLE,
    IF_VSHADER, IF_VDECL, IF_STATEBLOCK, IF_SWAPCHAIN, IF_QUERY,
    IF_DI8, IF_DIDEV, IF_DS, IF_DSBUF, IF_DIEFF, IF_EFFECT
};

static uint32_t g_vtab[NEL(g_if)];
static uint32_t g_com_ptr = COM_BASE;
static uint32_t g_d3d9 = 0;
static uint32_t g_dev = 0;

/* The guest's render, sound and main threads all create COM objects, so the
 * bump pointer and the lazily built vtables need a lock: without it two
 * threads were handed the same block and one zeroed the other's object (the
 * sound thread then called through a null vtable in sub_008c5d90). */
static pthread_mutex_t g_com_lock = PTHREAD_MUTEX_INITIALIZER;

static uint32_t com_alloc(uint32_t n) {
    n = (n + 15u) & ~15u;
    pthread_mutex_lock(&g_com_lock);
    if (g_com_ptr + n > COM_MAX) { pthread_mutex_unlock(&g_com_lock); return 0; }
    uint32_t p = g_com_ptr;
    g_com_ptr += n;
    mem_commit(p, n);
    memset(g_mem + p, 0, n);
    pthread_mutex_unlock(&g_com_lock);
    return p;
}

uint32_t com_new(uint32_t iface, uint32_t extra) {
    if (iface == 0 || iface >= NEL(g_if)) return 0;
    unsigned n = g_if[iface].n;
    static pthread_mutex_t vt_lock = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&vt_lock);
    uint32_t vt = g_vtab[iface];
    if (!vt) {
        vt = com_alloc(n * 4u);
        for (unsigned i = 0; i < n; i++) {
            MR32(vt + 4u * i) = COM_MAGIC | (iface << 16) | i;
        }
        g_vtab[iface] = vt;
    }
    pthread_mutex_unlock(&vt_lock);
    uint32_t blk = com_alloc(16u + 4u + extra);
    if (!blk) return 0;
    uint32_t obj = blk + 16u;
    MR32(obj) = vt;
    return obj;
}

void com_init(void) {
    mem_commit(COM_BASE, 0x10000);
}

static void fill_caps(uint32_t p) {
    mem_commit(p, 0x130);
    memset(g_mem + p, 0, 0x130);
    MR32(p+0x00)=1; MR32(p+0x08)=0x00020000; MR32(p+0x0C)=0x00000002;
    MR32(p+0x10)=0x00000020; MR32(p+0x14)=0x80000001; MR32(p+0x18)=1;
    MR32(p+0x1C)=0x001BBEF0; MR32(p+0x20)=0x0000033F; MR32(p+0x24)=0x0F730191;
    MR32(p+0x28)=0x000000FF; MR32(p+0x2C)=0x00003FFF; MR32(p+0x30)=0x00003FFF;
    MR32(p+0x34)=0x000000FF; MR32(p+0x38)=0x0008FFFF; MR32(p+0x3C)=0x0021EDA5;
    MR32(p+0x40)=0x07030700; MR32(p+0x44)=0x07030700; MR32(p+0x48)=0x07030700;
    MR32(p+0x4C)=0x0000003F; MR32(p+0x50)=0x0000003F; MR32(p+0x54)=0x0000001F;
    MR32(p+0x58)=4096; MR32(p+0x5C)=4096; MR32(p+0x60)=256; MR32(p+0x64)=8192;
    MR32(p+0x68)=4096; MR32(p+0x6C)=16;
    MRF32(p+0x70)=1.0e10f; MRF32(p+0x74)=-32768.0f; MRF32(p+0x78)=-32768.0f;
    MRF32(p+0x7C)=32768.0f; MRF32(p+0x80)=32768.0f; MRF32(p+0x84)=0.0f;
    MR32(p+0x88)=0x000001FF; MR32(p+0x8C)=0x00100008; MR32(p+0x90)=0x03FFFFFF;
    MR32(p+0x94)=8; MR32(p+0x98)=8; MR32(p+0x9C)=0x0000013B;
    MR32(p+0xA0)=8; MR32(p+0xA4)=6; MR32(p+0xA8)=4; MR32(p+0xAC)=8;
    MRF32(p+0xB0)=256.0f; MR32(p+0xB4)=1048575; MR32(p+0xB8)=1048575;
    MR32(p+0xBC)=16; MR32(p+0xC0)=508;
    MR32(p+0xC4)=0xFFFE0200u; /* VS 2.0 */
    MR32(p+0xC8)=256;
    MR32(p+0xCC)=0xFFFF0200u; /* PS 2.0 */
    MRF32(p+0xD0)=8.0f;
    MR32(p+0xD4)=0x00000051;
}

uint32_t com_direct3dcreate9(void) {
    if (!g_d3d9) {
        g_d3d9 = com_new(IF_D3D9, 64);
        fprintf(stderr, "com: IDirect3D9 created 0x%08x\n", g_d3d9);
    }
    return g_d3d9;
}

uint32_t com_get_device(void) {
    if (!g_dev) g_dev = com_new(IF_DEV, 256);
    return g_dev;
}

uint32_t com_create_effect(void) {
    return com_new(IF_EFFECT, 256);
}

/* A vertex or pixel shader object for bytecode already in guest memory (the
 * effects create their shaders this way, see d3dx_effect.c). */
uint32_t com_new_shader(uint32_t bytecode, uint32_t size) {
    uint32_t obj = com_new(IF_VSHADER, 64);
    if (obj) { MR32(obj + 4) = bytecode; MR32(obj + 8) = size; }
    return obj;
}

uint32_t com_directinput8create(uint32_t ppvOut) {
    uint32_t di8 = com_new(IF_DI8, 64);
    if (ppvOut && mem_is_committed(ppvOut)) {
        MR32(ppvOut) = di8;
    }
    fprintf(stderr, "com: IDirectInput8 created 0x%08x\n", di8);
    return 0; /* DI_OK */
}

/* ---- Direct3D resources ----------------------------------------------
 * Nothing is rendered by this bridge yet, but the game fills its textures and
 * buffers through Lock, so each resource needs real, correctly sized storage.
 * Pixel and vertex data live in the guest heap (the COM region is a small
 * bump allocator for the objects themselves) and are allocated on first lock.
 * When the last reference goes the data is freed; a later lock just
 * allocates it again, so a miscounted reference cannot leave a dangling
 * pointer behind.
 *
 * Fields after the vtable pointer (objects are created with 64 spare bytes): */
#define R_W      0x04u   /* width; byte length for vertex/index buffers */
#define R_H      0x08u
#define R_LEVELS 0x0Cu
#define R_FMT    0x10u
#define R_FACES  0x14u   /* 1, or 6 for a cube texture */
#define R_DATA   0x18u   /* texture: table of faces*levels data pointers; surface/buffer: the data */
#define R_REF    0x1Cu
#define R_PARENT 0x20u   /* surface: the texture it is a level of, or 0 */
#define R_INDEX  0x24u   /* surface: face*levels + level in the parent */

static uint32_t res_alloc(uint32_t n) {
    uint32_t p = gh_alloc(n ? n : 16u, 1, 16);
    if (!p) {
        static int warned;
        if (!warned++) fprintf(stderr, "com: guest heap exhausted allocating %u bytes of resource data\n", n);
    }
    return p;
}

static uint32_t mip(uint32_t d, uint32_t level) {
    d >>= level;
    return d ? d : 1u;
}

/* Bytes per pixel of an uncompressed D3DFORMAT, or 0 for the DXTn block formats. */
static uint32_t fmt_bytes(uint32_t fmt) {
    switch (fmt) {
    case 0x31545844u: case 0x32545844u: case 0x33545844u:
    case 0x34545844u: case 0x35545844u: return 0;                 /* DXT1..DXT5 */
    case 20: return 3;                                            /* R8G8B8 */
    case 23: case 24: case 25: case 26: case 29: case 30:         /* 16-bit RGB */
    case 40: case 51: case 60: case 70: case 80: case 81:         /* A8P8 A8L8 V8U8 D16_LOCKABLE D16 L16 */
    case 101: case 111: return 2;                                 /* INDEX16 R16F */
    case 27: case 28: case 41: case 50: case 52: return 1;        /* R3G3B2 A8 P8 L8 A4L4 */
    case 36: case 113: case 115: return 8;                        /* A16B16G16R16(F) G32R32F */
    case 116: return 16;                                          /* A32B32G32R32F */
    default: return 4;
    }
}

static void level_layout(uint32_t fmt, uint32_t w, uint32_t h, uint32_t *pitch, uint32_t *size) {
    uint32_t bpp = fmt_bytes(fmt);
    if (bpp) {
        *pitch = w * bpp;
        *size = *pitch * h;
    } else {
        uint32_t block = (fmt == 0x31545844u) ? 8u : 16u;
        *pitch = ((w + 3u) / 4u) * block;
        *size = *pitch * ((h + 3u) / 4u);
    }
}

static uint32_t full_chain(uint32_t w, uint32_t h) {
    uint32_t n = 1;
    while (w > 1 || h > 1) { w >>= 1; h >>= 1; n++; }
    return n;
}

static uint32_t tex_new(uint32_t iface, uint32_t w, uint32_t h, uint32_t levels, uint32_t fmt, uint32_t faces) {
    uint32_t obj = com_new(iface, 64);
    if (!obj) return 0;
    if (!w) w = 1;
    if (!h) h = 1;
    if (!levels || levels > full_chain(w, h)) levels = full_chain(w, h);
    MR32(obj + R_W) = w; MR32(obj + R_H) = h; MR32(obj + R_LEVELS) = levels;
    MR32(obj + R_FMT) = fmt; MR32(obj + R_FACES) = faces;
    MR32(obj + R_DATA) = res_alloc(4u * faces * levels);
    MR32(obj + R_REF) = 1;
    return obj;
}

/* Storage of one level of a texture, allocated on first use.  0 if out of range. */
static uint32_t tex_level(uint32_t tex, uint32_t index, uint32_t *pitch) {
    uint32_t levels = MR32(tex + R_LEVELS), faces = MR32(tex + R_FACES), tab = MR32(tex + R_DATA);
    if (!tab || !levels || index >= levels * faces) return 0;
    uint32_t level = index % levels, size;
    level_layout(MR32(tex + R_FMT), mip(MR32(tex + R_W), level), mip(MR32(tex + R_H), level), pitch, &size);
    if (!MR32(tab + 4u * index)) MR32(tab + 4u * index) = res_alloc(size);
    return MR32(tab + 4u * index);
}

/* Debugging aid: PROSTREET_TEXDUMP=N writes the top level of every texture
 * an effect binds during frame N to out/texdump/ as PPM (colour) plus PGM
 * (alpha), so one can see what the game is showing before draws are rendered.
 * The files are game data: they stay in the run directory. */
static uint64_t g_frame_no;
static void tex_dump(uint32_t tex) {
    static long want = -2; static unsigned serial;
    if (want == -2) { const char *e = getenv("PROSTREET_TEXDUMP"); want = e ? atol(e) : -1; }
    if (want < 0 || (long)g_frame_no != want || !tex || !mem_is_committed(tex + R_DATA)) return;
    uint32_t w = MR32(tex + R_W), h = MR32(tex + R_H), fmt = MR32(tex + R_FMT), pitch = 0;
    if (!w || !h || w > 4096 || h > 4096) return;
    uint32_t d = tex_level(tex, 0, &pitch);
    if (!d) return;
    uint8_t *rgb = malloc((size_t)w * h * 3), *al = malloc((size_t)w * h);
    const uint8_t *src = g_mem + d;
    int dxt = fmt == 0x31545844u ? 1 : fmt == 0x33545844u ? 3 : fmt == 0x35545844u ? 5 : 0;
    for (uint32_t y = 0; y < h; y++) for (uint32_t x = 0; x < w; x++) {
        uint8_t r = 0, g = 0, b = 0, a = 255;
        if (dxt) {
            const uint8_t *blk = src + ((size_t)(y / 4) * ((w + 3) / 4) + x / 4) * (dxt == 1 ? 8 : 16);
            const uint8_t *cb = dxt == 1 ? blk : blk + 8;
            unsigned c0 = cb[0] | cb[1] << 8, c1 = cb[2] | cb[3] << 8, k = (cb[4 + (y & 3)] >> ((x & 3) * 2)) & 3;
            int p[4][3] = { { (c0 >> 11) * 255 / 31, ((c0 >> 5) & 63) * 255 / 63, (c0 & 31) * 255 / 31 },
                            { (c1 >> 11) * 255 / 31, ((c1 >> 5) & 63) * 255 / 63, (c1 & 31) * 255 / 31 } };
            for (int i = 0; i < 3; i++) {
                if (c0 > c1 || dxt != 1) { p[2][i] = (2 * p[0][i] + p[1][i]) / 3; p[3][i] = (p[0][i] + 2 * p[1][i]) / 3; }
                else { p[2][i] = (p[0][i] + p[1][i]) / 2; p[3][i] = 0; }
            }
            r = (uint8_t)p[k][0]; g = (uint8_t)p[k][1]; b = (uint8_t)p[k][2];
            unsigned ai = (y & 3) * 4 + (x & 3);
            if (dxt == 3) a = (uint8_t)(((blk[ai / 2] >> ((ai & 1) * 4)) & 15) * 17);
            if (dxt == 5) {
                unsigned a0 = blk[0], a1 = blk[1]; uint64_t bits = 0;
                for (int i = 0; i < 6; i++) bits |= (uint64_t)blk[2 + i] << (8 * i);
                unsigned c = (unsigned)(bits >> (3 * ai)) & 7;
                a = (uint8_t)(c == 0 ? a0 : c == 1 ? a1 : a0 > a1 ? ((8 - c) * a0 + (c - 1) * a1) / 7
                              : c == 6 ? 0 : c == 7 ? 255 : ((6 - c) * a0 + (c - 1) * a1) / 5);
            }
        } else {
            const uint8_t *px = src + (size_t)y * pitch + (size_t)x * fmt_bytes(fmt);
            switch (fmt) {
            case 21: a = px[3]; /* fall through */        /* A8R8G8B8 */
            case 22: b = px[0]; g = px[1]; r = px[2]; break;   /* X8R8G8B8 */
            case 50: r = g = b = px[0]; break;            /* L8 */
            case 28: r = g = b = 255; a = px[0]; break;   /* A8 */
            case 51: r = g = b = px[0]; a = px[1]; break; /* A8L8 */
            default: r = g = b = px[0]; break;
            }
        }
        size_t i = (size_t)y * w + x;
        rgb[3 * i] = r; rgb[3 * i + 1] = g; rgb[3 * i + 2] = b; al[i] = a;
    }
    char name[160];
    mkdir("out/texdump", 0755);
    snprintf(name, sizeof name, "out/texdump/f%ld_%03u_%08x_%ux%u_fmt%x.ppm", want, serial, tex, w, h, fmt);
    FILE *f = fopen(name, "wb");
    if (f) { fprintf(f, "P6\n%u %u\n255\n", w, h); fwrite(rgb, 3, (size_t)w * h, f); fclose(f); }
    snprintf(name, sizeof name, "out/texdump/f%ld_%03u_%08x_alpha.pgm", want, serial++, tex);
    f = fopen(name, "wb");
    if (f) { fprintf(f, "P5\n%u %u\n255\n", w, h); fwrite(al, 1, (size_t)w * h, f); fclose(f); }
    free(rgb); free(al);
}

static uint32_t surf_new(uint32_t w, uint32_t h, uint32_t fmt, uint32_t parent, uint32_t index) {
    uint32_t obj = com_new(IF_SURF, 64);
    if (!obj) return 0;
    MR32(obj + R_W) = w; MR32(obj + R_H) = h; MR32(obj + R_FMT) = fmt;
    MR32(obj + R_PARENT) = parent; MR32(obj + R_INDEX) = index;
    MR32(obj + R_REF) = 1;
    return obj;
}

static uint32_t tex_surface(uint32_t tex, uint32_t index) {
    uint32_t levels = MR32(tex + R_LEVELS);
    uint32_t level = levels ? index % levels : 0;
    return surf_new(mip(MR32(tex + R_W), level), mip(MR32(tex + R_H), level), MR32(tex + R_FMT), tex, index);
}

/* ---- accessors for the software renderer (swrast.c) ---- */
uint32_t com_tex_level0(uint32_t tex, uint32_t *w, uint32_t *h, uint32_t *fmt, uint32_t *pitch) {
    *w = *h = *fmt = *pitch = 0;
    if (!tex || !mem_is_committed(tex + R_INDEX + 3) || MR32(tex) != g_vtab[IF_TEX]) return 0;
    *w = MR32(tex + R_W); *h = MR32(tex + R_H); *fmt = MR32(tex + R_FMT);
    return tex_level(tex, 0, pitch);
}
uint32_t com_surf_data(uint32_t surf, uint32_t *w, uint32_t *h, uint32_t *fmt, uint32_t *pitch) {
    *w = *h = *fmt = *pitch = 0;
    if (!surf || !mem_is_committed(surf + R_INDEX + 3) || MR32(surf) != g_vtab[IF_SURF]) return 0;
    *w = MR32(surf + R_W); *h = MR32(surf + R_H); *fmt = MR32(surf + R_FMT);
    if (!*w || !*h) return 0;
    if (MR32(surf + R_PARENT)) return tex_level(MR32(surf + R_PARENT), MR32(surf + R_INDEX), pitch);
    uint32_t size;
    level_layout(*fmt, *w, *h, pitch, &size);
    if (!MR32(surf + R_DATA)) MR32(surf + R_DATA) = res_alloc(size);
    return MR32(surf + R_DATA);
}
uint32_t com_buf_data(uint32_t buf, uint32_t *len, uint32_t *fmt) {
    *len = *fmt = 0;
    if (!buf || !mem_is_committed(buf + R_INDEX + 3) || (MR32(buf) != g_vtab[IF_VB] && MR32(buf) != g_vtab[IF_IB])) return 0;
    *len = MR32(buf + R_W); *fmt = MR32(buf + R_FMT);
    return MR32(buf + R_DATA);
}

/* The device's own surfaces and current targets. */
static uint32_t g_bb_w = 1280, g_bb_h = 720, g_backbuf, g_depth, g_cur_rt, g_cur_ds;
static uint32_t dev_backbuffer(void) {
    if (!g_backbuf) { g_backbuf = surf_new(g_bb_w, g_bb_h, 22, 0, 0); g_depth = surf_new(g_bb_w, g_bb_h, 75, 0, 0); }
    return g_backbuf;
}

/* The last reference to a resource went: drop its data. */
static void res_free_data(uint32_t iface, uint32_t obj) {
    if (iface == IF_TEX || iface == IF_CUBETEX) {
        uint32_t tab = MR32(obj + R_DATA), n = MR32(obj + R_LEVELS) * MR32(obj + R_FACES);
        for (uint32_t i = 0; tab && i < n; i++) {
            if (MR32(tab + 4u * i)) { gh_free(MR32(tab + 4u * i)); MR32(tab + 4u * i) = 0; }
        }
    } else if (MR32(obj + R_DATA)) {
        gh_free(MR32(obj + R_DATA));
        MR32(obj + R_DATA) = 0;
    }
}

static int res_counted(uint32_t iface) {
    return iface == IF_TEX || iface == IF_CUBETEX || iface == IF_SURF || iface == IF_VB || iface == IF_IB;
}

static void fill_surface_desc(uint32_t pd, uint32_t fmt, uint32_t w, uint32_t h, uint32_t usage) {
    if (!pd || !mem_is_committed(pd)) return;
    mem_commit(pd, 32);
    MR32(pd + 0) = fmt;
    MR32(pd + 4) = 1;      /* D3DRTYPE_SURFACE */
    MR32(pd + 8) = usage;
    MR32(pd + 12) = 1;     /* D3DPOOL_MANAGED */
    MR32(pd + 16) = 0;     /* D3DMULTISAMPLE_NONE */
    MR32(pd + 20) = 0;
    MR32(pd + 24) = w;
    MR32(pd + 28) = h;
}

/* D3DLOCKED_RECT for a level; pRect selects a sub-rectangle (block-aligned for DXTn). */
static void fill_locked_rect(uint32_t plr, uint32_t prect, uint32_t fmt, uint32_t data, uint32_t pitch) {
    if (!plr || !mem_is_committed(plr)) return;
    mem_commit(plr, 8);
    uint32_t off = 0;
    if (data && prect && mem_is_committed(prect)) {
        uint32_t left = MR32(prect), top = MR32(prect + 4), bpp = fmt_bytes(fmt);
        off = bpp ? top * pitch + left * bpp
                  : (top / 4u) * pitch + (left / 4u) * ((fmt == 0x31545844u) ? 8u : 16u);
    }
    MR32(plr + 0) = pitch;
    MR32(plr + 4) = data ? data + off : 0;
}

uint32_t com_directsoundcreate(uint32_t ppDS) {
    uint32_t ds = com_new(IF_DS, 64);
    if (ppDS && mem_is_committed(ppDS)) MR32(ppDS) = ds;
    fprintf(stderr, "com: IDirectSound created 0x%08x\n", ds);
    return 0; /* DS_OK */
}

#define COM_A(i) MR32(c->esp + 4u * (uint32_t)(i))

extern int sw_enabled(void);
extern void sw_dev_call(CPU *c, const char *nm);

int com_call(CPU *c, uint32_t magic) {
    POP32(c->eip);
    uint32_t iface = (magic >> 16) & 0xFFFu;
    uint32_t m = magic & 0xFFFFu;

    if (iface == 0 || iface >= NEL(g_if) || m >= g_if[iface].n) {
        fprintf(stderr, "  [COM] invalid interface/method %u:%u\n", iface, m);
        c->eax = 0x80004001u;
        return 1;
    }

    const Method *M = &g_if[iface].m[m];
    int nargs = (int)M->nargs;
    uint32_t r = 0;
    const char *nm = M->name;

    /* IUnknown methods common to all interfaces */
    if (m == 0) { /* QueryInterface(this, riid, ppv) */
        uint32_t ppv = COM_A(2);
        if (ppv && mem_is_committed(ppv)) MR32(ppv) = COM_A(0);
        r = 0;
    } else if (m == 1) { /* AddRef(this) */
        r = res_counted(iface) ? ++MR32(COM_A(0) + R_REF) : 1;
    } else if (m == 2) { /* Release(this) */
        r = 0;
        if (res_counted(iface)) {
            uint32_t self = COM_A(0);
            if (MR32(self + R_REF)) MR32(self + R_REF)--;
            r = MR32(self + R_REF);
            if (!r) res_free_data(iface, self);
        }
    } else if (iface == IF_DEV && sw_enabled() && (sw_dev_call(c, nm), 0)) {
    } else if (iface == IF_D3D9) {
        if (!strcmp(nm, "GetDeviceCaps")) {
            uint32_t pc = COM_A(3);
            if (pc) fill_caps(pc);
            r = 0;
        } else if (!strcmp(nm, "GetAdapterIdentifier")) {
            uint32_t p = COM_A(3);
            if (p) {
                mem_commit(p, 1100);
                memset(g_mem + p, 0, 1100);
                snprintf((char *)(g_mem + p), 512, "nv4_disp.dll");
                snprintf((char *)(g_mem + p + 512), 512, "NVIDIA GeForce 6800");
                snprintf((char *)(g_mem + p + 1024), 32, "\\\\.\\DISPLAY1");
                MR32(p + 1056) = 0x00000A55u;
                MR32(p + 1060) = 0x00060000u;
                MR32(p + 1064) = 0x10DEu; /* NVIDIA */
                MR32(p + 1068) = 0x0040u; /* GeForce 6800 */
                MR32(p + 1076) = 0xA1u;
                MR32(p + 1096) = 1u;
            }
            r = 0;
        } else if (!strcmp(nm, "GetAdapterCount")) {
            r = 1;
        } else if (!strcmp(nm, "GetAdapterModeCount")) {
            r = 1;
        } else if (!strcmp(nm, "EnumAdapterModes")) {
            uint32_t pm = COM_A(4);
            if (pm) {
                mem_commit(pm, 16);
                MR32(pm) = 1280; MR32(pm + 4) = 720; MR32(pm + 8) = 60; MR32(pm + 12) = 22;
            }
            r = 0;
        } else if (!strcmp(nm, "GetAdapterDisplayMode")) {
            uint32_t pm = COM_A(2);
            if (pm) {
                mem_commit(pm, 16);
                MR32(pm) = 1280; MR32(pm + 4) = 720; MR32(pm + 8) = 60; MR32(pm + 12) = 22;
            }
            r = 0;
        } else if (!strcmp(nm, "CheckDeviceType") || !strcmp(nm, "CheckDeviceFormat") ||
                   !strcmp(nm, "CheckDepthStencilMatch") || !strcmp(nm, "CheckDeviceFormatConversion")) {
            r = 0;
        } else if (!strcmp(nm, "CheckDeviceMultiSampleType")) {
            uint32_t q = COM_A(6);
            if (q && mem_is_committed(q)) MR32(q) = 1;
            r = 0;
        } else if (!strcmp(nm, "CreateDevice")) {
            if (!g_dev) g_dev = com_new(IF_DEV, 256);
            uint32_t pp = COM_A(6);
            /* D3DPRESENT_PARAMETERS: BackBufferWidth, BackBufferHeight first */
            if (COM_A(5) && mem_is_committed(COM_A(5) + 4) && MR32(COM_A(5)) && MR32(COM_A(5)) <= 8192 &&
                MR32(COM_A(5) + 4) && MR32(COM_A(5) + 4) <= 8192) { g_bb_w = MR32(COM_A(5)); g_bb_h = MR32(COM_A(5) + 4); }
            fprintf(stderr, "com: CreateDevice pp=0x%08x [pp]=0x%08x (0xac6ed4=0x%08x)\n",
                    pp, (pp && mem_is_committed(pp)) ? MR32(pp) : 0, MR32(0xac6ed4));
            if (pp && mem_is_committed(pp)) MR32(pp) = g_dev;
            if (mem_is_committed(0x00bfc10c)) MR32(0x00bfc10c) = g_dev;
            if (mem_is_committed(0x00ac6ed4)) {
                uint32_t dev = MR32(0x00ac6ed4);
                if (dev && mem_is_committed(dev)) MR32(dev) = g_dev;
            }
            fprintf(stderr, "com: IDirect3DDevice9 created 0x%08x -> wrote to 0x%08x and 0xbfc10c, now [0xac6ed4]=0x%08x\n",
                    g_dev, pp, MR32(0xac6ed4));
            r = 0;
        } else {
            r = 0;
        }
    } else if (iface == IF_DEV) {
        if (!strcmp(nm, "GetDeviceCaps")) {
            uint32_t pc = COM_A(1);
            if (pc) fill_caps(pc);
            r = 0;
        } else if (!strcmp(nm, "GetCreationParameters")) {
            uint32_t p = COM_A(1);
            if (p) {
                mem_commit(p, 16);
                MR32(p) = 0; MR32(p + 4) = 1; MR32(p + 8) = 0; MR32(p + 12) = 0x40;
            }
            r = 0;
        } else if (!strcmp(nm, "GetDisplayMode")) {
            uint32_t pm = COM_A(2);
            if (pm) {
                mem_commit(pm, 16);
                MR32(pm) = 1280; MR32(pm + 4) = 720; MR32(pm + 8) = 60; MR32(pm + 12) = 22;
            }
            r = 0;
        } else if (!strcmp(nm, "CreateTexture")) {
            /* (this, Width, Height, Levels, Usage, Format, Pool, ppTexture, pSharedHandle) */
            uint32_t pp = COM_A(7);
            if (pp && mem_is_committed(pp)) MR32(pp) = tex_new(IF_TEX, COM_A(1), COM_A(2), COM_A(3), COM_A(5), 1);
            r = 0;
        } else if (!strcmp(nm, "CreateCubeTexture")) {
            /* (this, EdgeLength, Levels, Usage, Format, Pool, ppCubeTexture, pSharedHandle) */
            uint32_t pp = COM_A(6);
            if (pp && mem_is_committed(pp)) MR32(pp) = tex_new(IF_CUBETEX, COM_A(1), COM_A(1), COM_A(2), COM_A(4), 6);
            r = 0;
        } else if (!strcmp(nm, "CreateVertexBuffer") || !strcmp(nm, "CreateIndexBuffer")) {
            /* (this, Length, Usage, FVF|Format, Pool, ppBuffer, pSharedHandle) */
            uint32_t obj = com_new(nm[6] == 'V' ? IF_VB : IF_IB, 64);
            MR32(obj + R_W) = COM_A(1);
            MR32(obj + R_FMT) = COM_A(3);
            MR32(obj + R_REF) = 1;
            uint32_t pp = COM_A(5);
            if (pp && mem_is_committed(pp)) MR32(pp) = obj;
            r = 0;
        } else if (!strcmp(nm, "CreateRenderTarget") || !strcmp(nm, "CreateDepthStencilSurface")) {
            /* (this, Width, Height, Format, MultiSample, Quality, Lockable|Discard, ppSurface, pSharedHandle) */
            uint32_t pp = COM_A(7);
            if (pp && mem_is_committed(pp)) MR32(pp) = surf_new(COM_A(1), COM_A(2), COM_A(3), 0, 0);
            r = 0;
        } else if (!strcmp(nm, "CreateStateBlock")) {
            uint32_t pp = COM_A(2);
            if (pp && mem_is_committed(pp)) MR32(pp) = com_new(IF_STATEBLOCK, 64);
            r = 0;
        } else if (!strcmp(nm, "CreateQuery")) {
            uint32_t pp = COM_A(2);
            if (pp && mem_is_committed(pp)) MR32(pp) = com_new(IF_QUERY, 64);
            r = 0;
        } else if (!strcmp(nm, "CreateVertexDeclaration")) {
            /* keep a copy of the D3DVERTEXELEMENT9 array (8 bytes each, ends at stream 0xFF) */
            uint32_t el = COM_A(1), pp = COM_A(2), n = 0, obj = com_new(IF_VDECL, 64);
            while (el && n < 64 && mem_is_committed(el + 8 * n + 7) && MR16(el + 8 * n) != 0xFF) n++;
            if (obj && el && mem_is_committed(el + 8 * n + 7)) {
                uint32_t copy = res_alloc(8 * (n + 1));
                if (copy) { memcpy(g_mem + copy, g_mem + el, 8 * (n + 1)); MR32(obj + 4) = copy; MR32(obj + 8) = n; }
            }
            if (pp && mem_is_committed(pp)) MR32(pp) = obj;
            r = 0;
        } else if (!strcmp(nm, "CreateVertexShader") || !strcmp(nm, "CreatePixelShader")) {
            /* keep a copy of the bytecode, up to its END token */
            uint32_t fn = COM_A(1), pp = COM_A(2), n = 0, obj = 0;
            while (fn && n < 65536 && mem_is_committed(fn + 4 * n + 3) && MR32(fn + 4 * n) != 0x0000FFFFu) {
                uint32_t t = MR32(fn + 4 * n);
                n += ((t & 0xFFFFu) == 0xFFFEu) ? 1 + (t >> 16) : 1;      /* a comment may contain the END value */
            }
            if (fn && n < 65536 && mem_is_committed(fn + 4 * n + 3)) {
                uint32_t copy = res_alloc(4 * (n + 1));
                if (copy) { memcpy(g_mem + copy, g_mem + fn, 4 * (n + 1)); obj = com_new_shader(copy, 4 * (n + 1)); }
            }
            if (!obj) obj = com_new(IF_VSHADER, 64);
            if (pp && mem_is_committed(pp)) MR32(pp) = obj;
            r = 0;
        } else if (!strcmp(nm, "GetBackBuffer")) {
            /* (this, iSwapChain, iBackBuffer, Type, ppBackBuffer) */
            if (COM_A(4) && mem_is_committed(COM_A(4))) MR32(COM_A(4)) = dev_backbuffer();
            r = 0;
        } else if (!strcmp(nm, "GetRenderTarget")) {
            if (COM_A(2) && mem_is_committed(COM_A(2))) MR32(COM_A(2)) = g_cur_rt ? g_cur_rt : dev_backbuffer();
            r = 0;
        } else if (!strcmp(nm, "GetDepthStencilSurface")) {
            dev_backbuffer();
            if (COM_A(1) && mem_is_committed(COM_A(1))) MR32(COM_A(1)) = g_cur_ds ? g_cur_ds : g_depth;
            r = 0;
        } else if (!strcmp(nm, "SetRenderTarget")) {
            if (COM_A(1) == 0 && COM_A(2)) g_cur_rt = COM_A(2);
            r = 0;
        } else if (!strcmp(nm, "SetDepthStencilSurface")) {
            g_cur_ds = COM_A(1);
            r = 0;
        } else if (!strcmp(nm, "GetDirect3D")) {
            uint32_t pp = COM_A(1);
            if (pp && mem_is_committed(pp)) MR32(pp) = g_d3d9;
            r = 0;
        } else if (!strcmp(nm, "BeginScene") || m == 41) {
            static uint64_t s_begin_frames = 0;
            if (++s_begin_frames <= 10 || (s_begin_frames % 300) == 0) {
                fprintf(stderr, "  [D3D9] BeginScene (frame %llu)\n", (unsigned long long)s_begin_frames);
            }
            r = 0; /* D3D_OK */
        } else if (!strcmp(nm, "EndScene") || m == 42) {
            r = 0; /* D3D_OK */
        } else if (!strcmp(nm, "Present") || m == 17) {
            static uint64_t s_present_frames = 0;
            g_frame_no++;
            { extern void sw_frame(long); sw_frame((long)g_frame_no); }
            if (++s_present_frames <= 10 || (s_present_frames % 300) == 0) {
                extern uint32_t gh_brk(void);
                fprintf(stderr, "  [D3D9] Present (frame %llu)  heap top %08x\n", (unsigned long long)s_present_frames, gh_brk());
            }
            /* Pace the game at the display rate: nothing blocks in Present here, and
             * unthrottled it ran some 1,200 frames a second.  PROSTREET_NOVSYNC=1
             * turns the wait off. */
            {
                static struct timespec s_next;
                static int s_novsync = -1;
                if (s_novsync < 0) s_novsync = getenv("PROSTREET_NOVSYNC") != NULL;
                if (!s_novsync) {
                    struct timespec now; clock_gettime(CLOCK_MONOTONIC, &now);
                    if (s_next.tv_sec == 0 || now.tv_sec > s_next.tv_sec + 1) s_next = now;
                    s_next.tv_nsec += 16666667L;
                    if (s_next.tv_nsec >= 1000000000L) { s_next.tv_sec++; s_next.tv_nsec -= 1000000000L; }
                    if (now.tv_sec < s_next.tv_sec || (now.tv_sec == s_next.tv_sec && now.tv_nsec < s_next.tv_nsec))
                        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &s_next, NULL);
                    else s_next = now;
                }
            }
            r = 0; /* D3D_OK */
        } else if (!strcmp(nm, "Clear") || m == 43) {
            r = 0; /* D3D_OK */
        } else if (!strcmp(nm, "Reset") || m == 16) {
            r = 0; /* D3D_OK */
        } else if (!strcmp(nm, "TestCooperativeLevel") || m == 3) {
            r = 0; /* D3D_OK */
        } else {
            r = 0;
        }
    } else if (iface == IF_TEX || iface == IF_CUBETEX) {
        /* The cube methods take a face before the level. */
        uint32_t self = COM_A(0), cube = iface == IF_CUBETEX;
        uint32_t levels = MR32(self + R_LEVELS);
        uint32_t level = COM_A(1u + cube);
        uint32_t index = (cube ? COM_A(1) * levels : 0) + level;
        if (!strcmp(nm, "GetSurfaceLevel") || !strcmp(nm, "GetCubeMapSurface")) {
            uint32_t pp = COM_A(2u + cube);
            if (pp && mem_is_committed(pp)) MR32(pp) = tex_surface(self, index);
            r = 0;
        } else if (!strcmp(nm, "GetLevelDesc")) {
            level = COM_A(1);
            fill_surface_desc(COM_A(2), MR32(self + R_FMT), mip(MR32(self + R_W), level), mip(MR32(self + R_H), level), 0);
            r = 0;
        } else if (!strcmp(nm, "GetLevelCount")) {
            r = levels;
        } else if (!strcmp(nm, "LockRect")) {
            /* (this, [Face,] Level, pLockedRect, pRect, Flags) */
            uint32_t pitch = 0;
            uint32_t data = tex_level(self, index, &pitch);
            fill_locked_rect(COM_A(2u + cube), COM_A(3u + cube), MR32(self + R_FMT), data, pitch);
            r = data ? 0 : 0x8876086Cu; /* D3DERR_INVALIDCALL */
        } else {
            r = 0;
        }
    } else if (iface == IF_SURF) {
        uint32_t self = COM_A(0);
        uint32_t w = MR32(self + R_W), h = MR32(self + R_H), fmt = MR32(self + R_FMT);
        if (!w || !h) { w = 1280; h = 720; fmt = 22; }  /* a surface the device handed out: the back buffer */
        if (!strcmp(nm, "GetDesc")) {
            fill_surface_desc(COM_A(1), fmt, w, h, MR32(self + R_PARENT) ? 0 : 1 /* D3DUSAGE_RENDERTARGET */);
            r = 0;
        } else if (!strcmp(nm, "LockRect")) {
            /* (this, pLockedRect, pRect, Flags) */
            uint32_t pitch = 0, size = 0, data;
            if (MR32(self + R_PARENT)) {
                data = tex_level(MR32(self + R_PARENT), MR32(self + R_INDEX), &pitch);
            } else {
                level_layout(fmt, w, h, &pitch, &size);
                if (!MR32(self + R_DATA)) MR32(self + R_DATA) = res_alloc(size);
                data = MR32(self + R_DATA);
            }
            fill_locked_rect(COM_A(1), COM_A(2), fmt, data, pitch);
            r = data ? 0 : 0x8876086Cu;
        } else {
            r = 0;
        }
    } else if (iface == IF_VB || iface == IF_IB) {
        uint32_t self = COM_A(0);
        if (!strcmp(nm, "Lock")) {
            /* (this, OffsetToLock, SizeToLock, ppbData, Flags) */
            uint32_t offset = COM_A(1), ppb = COM_A(3);
            if (!MR32(self + R_DATA)) MR32(self + R_DATA) = res_alloc(MR32(self + R_W));
            uint32_t buf = MR32(self + R_DATA);
            if (ppb && mem_is_committed(ppb)) MR32(ppb) = buf ? buf + offset : 0;
            r = buf ? 0 : 0x8876086Cu;
        } else if (!strcmp(nm, "GetDesc")) {
            uint32_t pd = COM_A(1);
            if (pd && mem_is_committed(pd)) {
                mem_commit(pd, 24);
                MR32(pd + 0) = (iface == IF_IB) ? MR32(self + R_FMT) : 100; /* D3DFMT_INDEX16/32, D3DFMT_VERTEXDATA */
                MR32(pd + 4) = (iface == IF_IB) ? 7 : 6;   /* D3DRTYPE_INDEXBUFFER / VERTEXBUFFER */
                MR32(pd + 8) = 0;
                MR32(pd + 12) = 1;                         /* D3DPOOL_MANAGED */
                MR32(pd + 16) = MR32(self + R_W);
                if (iface == IF_VB) MR32(pd + 20) = MR32(self + R_FMT);  /* FVF */
            }
            r = 0;
        } else {
            r = 0;
        }
    } else if (iface == IF_DI8) {
        if (!strcmp(nm, "CreateDevice")) {
            /* (this, rguid, ppDevice, pUnkOuter); remember which system device it is */
            uint32_t rguid = COM_A(1), pp = COM_A(2);
            uint32_t dev = com_new(IF_DIDEV, 64);
            MR32(dev + 4) = (rguid && mem_is_committed(rguid) && MR8(rguid) == 0x60) ? 0x12u : 0x13u;
            if (pp && mem_is_committed(pp)) MR32(pp) = dev;
            r = 0;
        } else if (!strcmp(nm, "EnumDevices")) {
            /* (this, dwDevType, lpCallback, pvRef, dwFlags)
             * BOOL CALLBACK cb(const DIDEVICEINSTANCEA *, void *pvRef), stdcall.
             * Report the system keyboard and mouse: with no device at all the
             * game's input layer keeps null device objects and the frontend
             * faults on the first one it uses (sub_005a97f0). */
            static const struct { uint32_t type; uint8_t guid0; const char *name; } devs[] = {
                { 0x00000413u, 0x61, "Keyboard" },   /* DI8DEVTYPE_KEYBOARD, PC enhanced */
                { 0x00000112u, 0x60, "Mouse" },      /* DI8DEVTYPE_MOUSE */
            };
            static const uint8_t guid_tail[15] = { 0x2B, 0x1D, 0x6F, 0xA0, 0xD5, 0xCF, 0x11,
                                                   0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 };
            uint32_t want = COM_A(1), cb = COM_A(2), ref = COM_A(3), ret = c->eip;
            uint32_t inst = com_alloc(0x250);
            for (unsigned i = 0; i < NEL(devs) && cb && inst; i++) {
                uint32_t kind = devs[i].type & 0xFFu;                 /* 0x13 keyboard, 0x12 mouse */
                uint32_t cls = kind == 0x13u ? 3u : 2u;              /* DI8DEVCLASS_KEYBOARD / _POINTER */
                if (want && want != cls && want != kind) continue;   /* 0 = DI8DEVCLASS_ALL */
                memset(g_mem + inst, 0, 0x244);
                MR32(inst) = 0x244;                                   /* dwSize */
                for (unsigned g = 0; g < 2; g++) {                    /* guidInstance, guidProduct */
                    MR8(inst + 4 + 16 * g) = devs[i].guid0;
                    memcpy(g_mem + inst + 5 + 16 * g, guid_tail, 15);
                }
                MR32(inst + 0x24) = devs[i].type;                     /* dwDevType */
                snprintf((char *)(g_mem + inst + 0x28), 260, "%s", devs[i].name);
                snprintf((char *)(g_mem + inst + 0x12C), 260, "%s", devs[i].name);
                PUSH32(ref);
                PUSH32(inst);
                guest_call(c, cb);
                if (!c->eax) break;                                   /* DIENUM_STOP */
            }
            c->eip = ret;
            r = 0;
        } else {
            r = 0;
        }
    } else if (iface == IF_DIDEV) {
        uint32_t kind = MR32(COM_A(0) + 4);                           /* 0x13 keyboard, 0x12 mouse */
        if (!strcmp(nm, "GetCapabilities")) {
            /* DIDEVCAPS: dwSize, dwFlags, dwDevType, dwAxes, dwButtons, dwPOVs, ... */
            uint32_t p = COM_A(1);
            if (p && mem_is_committed(p)) {
                MR32(p + 4) = 1;                                      /* DIDC_ATTACHED */
                MR32(p + 8) = kind == 0x12u ? 0x00000112u : 0x00000413u;
                MR32(p + 12) = kind == 0x12u ? 3 : 0;
                MR32(p + 16) = kind == 0x12u ? 3 : 256;
                MR32(p + 20) = 0;
            }
            r = 0;
        } else if (!strcmp(nm, "GetDeviceState")) {
            uint32_t sz = COM_A(1);
            uint32_t p = COM_A(2);
            if (p) {
                mem_commit(p, sz);
                memset(g_mem + p, 0, sz);
                /* Scripted key presses for unattended runs:
                 *   PROSTREET_KEYS="600:28,900:28+30"
                 * holds DirectInput scan code 28 (Enter) for 10 frames from frame
                 * 600, and for 30 frames from frame 900.  Codes 256-263 are mouse
                 * buttons 0-7 (256 = left), held in the mouse state's rgbButtons. */
                static struct { long frame, len; int key; } keys[32];
                static int nkeys = -1;
                if (nkeys < 0) {
                    nkeys = 0;
                    const char *e = getenv("PROSTREET_KEYS");
                    while (e && *e && nkeys < 32) {
                        char *end;
                        keys[nkeys].frame = strtol(e, &end, 10);
                        if (*end != ':') break;
                        keys[nkeys].key = (int)strtol(end + 1, &end, 10);
                        keys[nkeys].len = 10;
                        if (*end == '+') keys[nkeys].len = strtol(end + 1, &end, 10);
                        nkeys++;
                        e = *end == ',' ? end + 1 : "";
                    }
                }
                static uint32_t seen_sz[4];
                for (int k = 0; k < 4 && seen_sz[k] != sz; k++)
                    if (!seen_sz[k]) { seen_sz[k] = sz; fprintf(stderr, "dinput: GetDeviceState(%u bytes) into %#x from %#x first read at frame %lu\n", sz, p, c->eip, (unsigned long)g_frame_no); break; }
                for (int i = 0; i < nkeys; i++) {
                    if ((long)g_frame_no < keys[i].frame || (long)g_frame_no >= keys[i].frame + keys[i].len) continue;
                    if (sz == 256 && keys[i].key < 256)
                        MR8(p + keys[i].key) = 0x80;
                    else if ((sz == 16 || sz == 20) && keys[i].key >= 256 && keys[i].key - 256 < (int)sz - 12)
                        MR8(p + 12 + (keys[i].key - 256)) = 0x80;     /* DIMOUSESTATE(2).rgbButtons */
                }
            }
            r = 0;
        } else if (!strcmp(nm, "GetDeviceData")) {
            static int logged;
            if (!logged++) fprintf(stderr, "dinput: GetDeviceData first call at frame %lu (kind %#x)\n", (unsigned long)g_frame_no, kind);
            uint32_t pq = COM_A(2);
            if (pq && mem_is_committed(pq)) MR32(pq) = 0;
            r = 0;
        } else {
            r = 0;
        }
    } else if (iface == IF_DS) {
        /* No audio output yet: buffers are plain guest memory that is never played. */
        if (!strcmp(nm, "CreateSoundBuffer")) {
            uint32_t desc = COM_A(1), pp = COM_A(2);
            /* DSBUFFERDESC: dwSize, dwFlags, dwBufferBytes, dwReserved, lpwfxFormat */
            uint32_t len = (desc && mem_is_committed(desc)) ? MR32(desc + 8) : 0;
            if (len == 0) len = 0x10000;     /* the primary buffer has no size of its own */
            uint32_t obj = com_new(IF_DSBUF, 64);
            MR32(obj + 4) = com_alloc(len);
            MR32(obj + 8) = len;
            /* WAVEFORMATEX.nAvgBytesPerSec drives the play cursor (see GetCurrentPosition) */
            uint32_t fmt = (desc && mem_is_committed(desc)) ? MR32(desc + 16) : 0;
            MR32(obj + 12) = (fmt && mem_is_committed(fmt) && MR32(fmt + 8)) ? MR32(fmt + 8) : 176400u;
            if (pp && mem_is_committed(pp)) MR32(pp) = obj;
            r = 0;
        } else if (!strcmp(nm, "GetCaps")) {
            /* DSCAPS: keep the caller's dwSize; a software device with no hardware
             * mixing.  The game takes its usable sample rates from the secondary
             * range (sub_008cbeb0), so a zero range leaves it with no rate at all. */
            uint32_t p = COM_A(1);
            if (p && mem_is_committed(p)) {
                uint32_t sz = MR32(p);
                if (sz >= 4 && sz <= 0x60) memset(g_mem + p + 4, 0, sz - 4);
                if (sz >= 0x14) {
                    MR32(p + 0x04) = 0x00000F1Fu; /* primary+secondary mono/stereo 8/16 bit, continuous rate */
                    MR32(p + 0x08) = 100;         /* dwMinSecondarySampleRate */
                    MR32(p + 0x0C) = 200000;      /* dwMaxSecondarySampleRate */
                    MR32(p + 0x10) = 1;           /* dwPrimaryBuffers */
                }
            }
            r = 0;
        } else if (!strcmp(nm, "GetSpeakerConfig")) {
            uint32_t p = COM_A(1);
            if (p && mem_is_committed(p)) MR32(p) = 4; /* DSSPEAKER_STEREO */
            r = 0;
        } else if (!strcmp(nm, "DuplicateSoundBuffer")) {
            uint32_t src = COM_A(1), pp = COM_A(2);
            uint32_t obj = com_new(IF_DSBUF, 64);
            if (src && mem_is_committed(src)) { MR32(obj + 4) = MR32(src + 4); MR32(obj + 8) = MR32(src + 8); MR32(obj + 12) = MR32(src + 12); }
            if (pp && mem_is_committed(pp)) MR32(pp) = obj;
            r = 0;
        } else {
            r = 0;
        }
    } else if (iface == IF_DSBUF) {
        uint32_t self = COM_A(0);
        uint32_t buf = MR32(self + 4), len = MR32(self + 8);
        if (!strcmp(nm, "Lock")) {
            /* Lock(this, offset, bytes, ppv1, pcb1, ppv2, pcb2, flags) */
            uint32_t off = COM_A(1), n = COM_A(2), flags = COM_A(7);
            if (flags & 2u) { off = 0; n = len; }      /* DSBLOCK_ENTIREBUFFER */
            if (len && off >= len) off %= len;
            if (n > len) n = len;
            uint32_t n1 = n > len - off ? len - off : n;   /* the rest wraps to the start */
            uint32_t p1 = COM_A(3), c1 = COM_A(4), p2 = COM_A(5), c2 = COM_A(6);
            if (p1 && mem_is_committed(p1)) MR32(p1) = buf + off;
            if (c1 && mem_is_committed(c1)) MR32(c1) = n1;
            if (p2 && mem_is_committed(p2)) MR32(p2) = (n > n1) ? buf : 0;
            if (c2 && mem_is_committed(c2)) MR32(c2) = n - n1;
            r = 0;
        } else if (!strcmp(nm, "Play") || !strcmp(nm, "Stop") || !strcmp(nm, "SetCurrentPosition") ||
                   !strcmp(nm, "GetCurrentPosition") || !strcmp(nm, "GetStatus")) {
            /* Nothing is played, but the play cursor moves in real time while the
             * buffer is "playing": the game's mixer thread (sub_008c5f20) only
             * mixes, and only drains its command queue, when the cursor has
             * advanced.  With a cursor stuck at 0 that queue grew without bound
             * and its writes ran over the import table.
             * obj: +12 bytes/second, +16 playing, +20 position at +24, +24 start ms */
            struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
            uint32_t now = (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
            uint32_t rate = MR32(self + 12) ? MR32(self + 12) : 176400u;
            uint32_t pos = MR32(self + 20);
            if (MR32(self + 16) && len)
                pos = (uint32_t)((pos + (uint64_t)(now - MR32(self + 24)) * rate / 1000u) % len);
            pos &= ~3u;
            if (!strcmp(nm, "Play")) {
                if (!MR32(self + 16)) { MR32(self + 16) = 1; MR32(self + 24) = now; }
            } else if (!strcmp(nm, "Stop")) {
                MR32(self + 16) = 0; MR32(self + 20) = pos;
            } else if (!strcmp(nm, "SetCurrentPosition")) {
                MR32(self + 20) = len ? COM_A(1) % len : 0; MR32(self + 24) = now;
            } else if (!strcmp(nm, "GetStatus")) {
                uint32_t p = COM_A(1);
                if (p && mem_is_committed(p)) MR32(p) = MR32(self + 16) ? 5u : 0u;  /* PLAYING|LOOPING */
            } else {
                uint32_t pp = COM_A(1), pw = COM_A(2);
                uint32_t lead = (rate / 100u) & ~3u;       /* write cursor 10 ms ahead */
                if (pp && mem_is_committed(pp)) MR32(pp) = pos;
                if (pw && mem_is_committed(pw)) MR32(pw) = len ? (pos + lead) % len : 0;
            }
            r = 0;
        } else if (!strcmp(nm, "GetCaps")) {
            /* DSBCAPS: dwSize, dwFlags, dwBufferBytes, dwUnlockTransferRate, dwPlayCpuOverhead */
            uint32_t p = COM_A(1);
            if (p && mem_is_committed(p)) { MR32(p + 4) = 0; MR32(p + 8) = len; MR32(p + 12) = 0; MR32(p + 16) = 0; }
            r = 0;
        } else if (!strcmp(nm, "GetVolume") ||
                   !strcmp(nm, "GetPan") || !strcmp(nm, "GetFrequency")) {
            uint32_t p = COM_A(1);
            if (p && mem_is_committed(p)) MR32(p) = 0;
            r = 0;
        } else if (!strcmp(nm, "GetFormat")) {
            uint32_t pn = COM_A(3);
            if (pn && mem_is_committed(pn)) MR32(pn) = 0;
            r = 0;
        } else {
            r = 0;
        }
    } else if (iface == IF_EFFECT) {
        extern int fx_call(CPU *c, uint32_t self, unsigned m, uint32_t *ret);
        if (!strcmp(nm, "SetTexture")) tex_dump(COM_A(2));
        if (fx_call(c, COM_A(0), m, &r)) {
            /* a real effect (d3dx_effect.c); what follows is the old stub, kept
             * for an effect whose resource could not be read */
        } else if (!strcmp(nm, "GetDesc") || m == 3) {
            uint32_t pd = COM_A(1);
            if (pd && mem_is_committed(pd)) {
                MR32(pd + 0) = 0x009f8ecc; /* Creator = "STANDARD" */
                MR32(pd + 4) = 0;          /* Parameters = 0 */
                MR32(pd + 8) = 0;          /* Techniques = 0 */
                MR32(pd + 12) = 0;         /* Functions = 0 */
            }
            r = 0;
        } else if (!strcmp(nm, "GetTechniqueDesc") || m == 5) {
            uint32_t pd = COM_A(2);
            if (pd && mem_is_committed(pd)) {
                MR32(pd + 0) = 0x009f8ecc; /* Name = "STANDARD" */
                MR32(pd + 4) = 1;          /* Passes = 1 */
                MR32(pd + 8) = 0;          /* Annotations = 0 */
            }
            r = 0;
        } else if (!strcmp(nm, "GetInt") || m == 27) {
            uint32_t pp = COM_A(2);
            if (pp && mem_is_committed(pp)) MR32(pp) = 0;
            r = 0;
        } else if (!strcmp(nm, "GetString") || m == 51) {
            uint32_t pp = COM_A(2);
            if (pp && mem_is_committed(pp)) MR32(pp) = 0x009f8ecc;
            r = 0;
        } else if (!strcmp(nm, "GetTechniqueByName") || !strcmp(nm, "GetTechnique")) {
            r = 0x1000;
        } else if (!strcmp(nm, "GetParameterByName") || !strcmp(nm, "GetParameter")) {
            r = 0x2000;
        } else if (!strcmp(nm, "FindNextValidTechnique") || !strcmp(nm, "FindNextValidTechnique72")) {
            uint32_t pp = COM_A(2);
            if (pp && mem_is_committed(pp)) MR32(pp) = 0x1000;
            r = 0;
        } else if (!strcmp(nm, "SetTechnique") || !strcmp(nm, "SetTechnique71") || m == 58 || m == 71) {
            r = 0;
        } else if (!strcmp(nm, "Begin") || !strcmp(nm, "Begin75") || m == 63 || m == 75) {
            uint32_t pPasses = COM_A(2);
            if (pPasses && mem_is_committed(pPasses)) MR32(pPasses) = 1; /* 1 pass */
            r = 0; /* D3D_OK */
        } else if (!strcmp(nm, "BeginPass") || !strcmp(nm, "BeginPass77") || !strcmp(nm, "Pass76") || m == 64 || m == 76 || m == 77) {
            r = 0; /* D3D_OK */
        } else if (!strcmp(nm, "CommitChanges") || m == 65) {
            r = 0; /* D3D_OK */
        } else if (!strcmp(nm, "EndPass") || !strcmp(nm, "EndPass79") || m == 66 || m == 79) {
            r = 0; /* D3D_OK */
        } else if (!strcmp(nm, "End") || !strcmp(nm, "End80") || m == 67 || m == 80) {
            r = 0; /* D3D_OK */
        } else if (!strcmp(nm, "SetRawValue") || m == 78) {
            r = 0;
        } else {
            r = 0;
        }
    } else {
        r = 0;
    }

    /* PROSTREET_COMTRACE=1 logs every COM method call with its result;
     * PROSTREET_COMTRACE=<substring> only the interfaces whose name contains it. */
    static int s_trace = -1;
    static const char *s_filter;
    if (s_trace < 0) {
        s_filter = getenv("PROSTREET_COMTRACE");
        s_trace = s_filter != 0;
        if (s_trace && !strcmp(s_filter, "1")) s_filter = "";
    }
    if (s_trace && strstr(g_if[iface].name, s_filter)) {
        fprintf(stderr, "  [COM] %s::%s(this=%08x", g_if[iface].name, nm, COM_A(0));
        for (int i = 1; i < nargs && i < 6; i++) fprintf(stderr, ", %08x", COM_A(i));
        fprintf(stderr, ") = %08x  ret=%08x\n", r, c->eip);
    }

    c->eax = r;
    c->esp += 4u * (uint32_t)nargs;
    { extern void rt_watch_check(CPU *, const char *, uint32_t); rt_watch_check(c, nm, magic); }
    return 1;
}

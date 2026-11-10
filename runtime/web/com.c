/* Minimal D3D9 COM bridge for the ProStreet browser port.
 *
 * The guest sees real COM objects: [obj+0] is a vtable whose entries are
 * magic values 0xE0000000 | (interface << 16) | method.  The lifted indirect
 * call hands that magic to dispatch_indirect(), which forwards it here.
 *
 * This is deliberately not nfsu2-web's com.c (4990 lines of NFSU2-specific
 * rendering).  It does exactly what S2 needs: create the objects the game
 * asks for, hand back plausible caps/descriptors, let it lock textures and
 * vertex buffers in guest memory, and record Clear/Present through the
 * gpu_web.c protocol.  Everything that is still a stub logs its name and
 * arguments once (AGENTS.md: a stub must say so).
 *
 * Calling convention: every D3D9 method is stdcall.  com_call() pops the
 * return address (the lifted call site pushed it) and, after the handler,
 * pops the arguments.  Method argument counts come from d3d9_tables.h.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "cpu.h"
#include "d3d9_tables.h"
#include "gpu_proto.h"

#define NEL(a) ((unsigned)(sizeof(a) / sizeof(*(a))))
#define COM_MAGIC 0xE0000000u

/* Allocated by web/host.c's guest bump allocator (inside GUEST_END). */
uint32_t host_guest_alloc(uint32_t size);

static void *G(uint32_t a) { return (void *)(uintptr_t)a; }
static void gzero(uint32_t a, uint32_t n) { memset(G(a), 0, n); }

typedef struct {
    const char *name;
    const Method *m;
    unsigned n;
} Iface;

/* Interfaces we can hand out.  The numbering matches nfsu2-web's g_if so
 * the tables in d3d9_tables.h stay 1:1 with it (easy to compare). */
static const Iface g_if[] = {
    {"?", 0, 0},
    {"IDirect3D9", M_D3D9, NEL(M_D3D9)},
    {"IDirect3DDevice9", M_DEVICE, NEL(M_DEVICE)},
    {"IDirect3DTexture9", M_TEXTURE, NEL(M_TEXTURE)},
    {"IDirect3DVertexBuffer9", M_VBUF, NEL(M_VBUF)},
    {"IDirect3DIndexBuffer9", M_IBUF, NEL(M_IBUF)},
    {"IDirect3DSurface9", M_SURFACE, NEL(M_SURFACE)},
    {"IDirect3DSimple", M_SIMPLE, NEL(M_SIMPLE)},
    {"IDirect3DVertexShader9", M_VSHADER, NEL(M_VSHADER)},
    {"IDirect3DVertexDeclaration9", M_VDECL, NEL(M_VDECL)},
    {"IDirect3DStateBlock9", M_STATEBLOCK, NEL(M_STATEBLOCK)},
    {"IDirect3DSwapChain9", M_SWAPCHAIN, NEL(M_SWAPCHAIN)},
    {"IDirect3DQuery9", M_QUERY, NEL(M_QUERY)},
    /* Cube textures share the 2D layout except GetCubeMapSurface/LockRect,
     * whose extra FaceType would leave the stdcall stack 4 bytes short. */
    {"IDirect3DCubeTexture9", M_TEXTURE, NEL(M_TEXTURE)},
    {"IDirect3DVolumeTexture9", M_TEXTURE, NEL(M_TEXTURE)},
};
enum {
    IF_D3D9 = 1, IF_DEV, IF_TEX, IF_VB, IF_IB, IF_SURF, IF_SIMPLE,
    IF_VSHADER, IF_VDECL, IF_STATEBLOCK, IF_SWAPCHAIN, IF_QUERY,
    IF_CUBETEX, IF_VOLTEX
};

#define MAX_OBJ 4096
#define MAX_LEVELS 16

typedef struct {
    uint32_t guest;        /* [vt][index] */
    int iface;
    int refs;
    uint32_t device;
    uint32_t parent;       /* container texture for surfaces, device for resources */
    uint32_t data;         /* guest bytes (texture/surface/vb/ib) or bytecode ptr */
    uint32_t bytes;
    uint32_t width, height, format, usage, pool, type, fvf, size, len;
    uint32_t levels, faces;
    uint32_t level_off[MAX_LEVELS];
    uint32_t level_bytes[MAX_LEVELS];
    uint32_t level_total;
    uint32_t lock_ptr;
    uint32_t swapchain;
    uint32_t rt[4];
    uint32_t depth;
    uint32_t tex[16];
    uint32_t decl, vs, ps;
    uint32_t streams[16];
    uint32_t stream_off[16], stream_stride[16];
    uint32_t indices;
    int is_backbuffer, is_depth;
} Obj;

static Obj g_obj[MAX_OBJ];
static unsigned g_obj_n;
static uint32_t g_vtab[NEL(g_if)];
static uint32_t g_d3d9, g_dev, g_swap, g_bb, g_depth;
static unsigned g_presented, g_cleared, g_draws;
static unsigned g_logged[NEL(g_if)][200];

static uint32_t com_alloc(uint32_t n)
{
    uint32_t p = host_guest_alloc(n ? n : 1u);
    if (p)
        gzero(p, n);
    return p;
}

static uint32_t com_new(int iface, uint32_t extra_bytes)
{
    (void)extra_bytes;
    if (!iface || iface >= (int)NEL(g_if) || g_obj_n >= MAX_OBJ)
        return 0;
    uint32_t vt = g_vtab[iface];
    if (!vt) {
        vt = com_alloc(g_if[iface].n * 4u);
        if (!vt)
            return 0;
        for (unsigned i = 0; i < g_if[iface].n; ++i)
            MR32(vt + i * 4u) = COM_MAGIC | ((uint32_t)iface << 16) | i;
        g_vtab[iface] = vt;
    }
    unsigned idx = g_obj_n++;
    uint32_t obj = com_alloc(8u);
    if (!obj) {
        g_obj_n--;
        return 0;
    }
    MR32(obj) = vt;
    MR32(obj + 4u) = idx;
    Obj *o = &g_obj[idx];
    memset(o, 0, sizeof *o);
    o->guest = obj;
    o->iface = iface;
    o->refs = 1;
    return obj;
}

static Obj *obj_of(uint32_t p)
{
    if (!p)
        return 0;
    uint32_t idx = MR32(p + 4u);
    if (idx >= g_obj_n || g_obj[idx].guest != p)
        return 0;
    return &g_obj[idx];
}

static void obj_note(int iface, const char *name, CPU *c, int nargs, int impl)
{
    unsigned m = 0;
    if (iface >= 0 && iface < (int)NEL(g_if)) {
        const Iface *I = &g_if[iface];
        while (m < I->n && strcmp(I->m[m].name, name) != 0)
            m++;
        if (m < I->n) {
            if (g_logged[iface][m < NEL(g_logged[0]) ? m : 0]++)
                return;
        }
    }
    if (impl)
        return;
    fprintf(stderr, "[com] stub %s::%s", g_if[iface].name, name);
    for (int i = 1; i < nargs && i < 6; ++i)
        fprintf(stderr, " %08x", MR32(c->esp + 4u * (uint32_t)i));
    fprintf(stderr, "\n");
}

/* ---- format helpers ---- */

static uint32_t fmt_rowbytes(uint32_t f, uint32_t w)
{
    if (f == 0x31545844u)                    /* DXT1 */
        return ((w + 3u) / 4u) * 8u;
    if (f >= 0x32545844u && f <= 0x35545844u) /* DXT2..DXT5 */
        return ((w + 3u) / 4u) * 16u;
    switch (f) {
    case 20:                                 /* R8G8B8 */
        return w * 3u;
    case 23: case 24: case 25: case 26: case 29: case 30:
    case 34: case 50: case 51: case 52: case 60: case 61:
    case 70: case 73: case 80: case 81: case 111:
        return w * 2u;
    case 113: case 115: case 116:            /* 64-bit float formats */
        return w * 8u;
    default:
        return w * 4u;
    }
}

static uint32_t fmt_bpp(uint32_t f)
{
    uint32_t w = fmt_rowbytes(f, 4u);
    return w / 4u;
}

static uint32_t tex_level_size(uint32_t f, uint32_t w, uint32_t h)
{
    if (f == 0x31545844u || (f >= 0x32545844u && f <= 0x35545844u))
        return fmt_rowbytes(f, w) * ((h + 3u) / 4u);
    return fmt_rowbytes(f, w) * h;
}

static uint32_t pow2_max(uint32_t v)
{
    uint32_t p = 1;
    while (p * 2u <= v)
        p *= 2u;
    return p;
}

/* Lay out the mip chain (and cube faces, if any) inside one guest buffer. */
static int tex_layout(Obj *o, uint32_t w, uint32_t h, uint32_t levels,
                      uint32_t format, uint32_t faces)
{
    if (!w || !h)
        return 0;
    if (levels == 0) {
        uint32_t n = 1, ww = w, hh = h;
        while (ww > 1u || hh > 1u) {
            ww = ww > 1u ? ww / 2u : 1u;
            hh = hh > 1u ? hh / 2u : 1u;
            n++;
        }
        levels = n;
    }
    if (levels > MAX_LEVELS)
        levels = MAX_LEVELS;
    o->width = w;
    o->height = h;
    o->format = format;
    o->levels = levels;
    o->faces = faces ? faces : 1u;
    uint32_t total = 0;
    for (uint32_t l = 0; l < levels; ++l) {
        uint32_t lw = w >> l, lh = h >> l;
        if (!lw) lw = 1;
        if (!lh) lh = 1;
        uint32_t sz = tex_level_size(format, lw, lh);
        sz = (sz + 3u) & ~3u;
        o->level_off[l] = total;
        o->level_bytes[l] = sz;
        total += sz;
    }
    o->level_total = total;
    o->bytes = total * o->faces;
    o->data = com_alloc(o->bytes);
    return o->data != 0;
}

static void fill_caps(uint32_t p)
{
    if (!p)
        return;
    gzero(p, 0x130);
    MR32(p + 0x00) = 1;                      /* DeviceType = HAL */
    MR32(p + 0x04) = 0;
    MR32(p + 0x08) = 0x00020000;
    MR32(p + 0x0C) = 0x00000002;
    MR32(p + 0x10) = 0x00000020;
    MR32(p + 0x14) = 0x80000001;
    MR32(p + 0x18) = 1;
    MR32(p + 0x1C) = 0x001BBEF0;
    MR32(p + 0x20) = 0x0000033F;
    MR32(p + 0x24) = 0x0F730191;
    MR32(p + 0x28) = 0x000000FF;
    MR32(p + 0x2C) = 0x00003FFF;
    MR32(p + 0x30) = 0x00003FFF;
    MR32(p + 0x34) = 0x000000FF;
    MR32(p + 0x38) = 0x0008FFFF;
    MR32(p + 0x3C) = 0x0021EDA5;
    MR32(p + 0x40) = 0x07030700;
    MR32(p + 0x44) = 0x07030700;
    MR32(p + 0x48) = 0x07030700;
    MR32(p + 0x4C) = 0x0000003F;
    MR32(p + 0x50) = 0x0000003F;
    MR32(p + 0x54) = 0x0000001F;
    MR32(p + 0x58) = 8192;                   /* MaxTextureWidth */
    MR32(p + 0x5C) = 8192;                   /* MaxTextureHeight */
    MR32(p + 0x60) = 256;
    MR32(p + 0x64) = 8192;
    MR32(p + 0x68) = 8192;
    MR32(p + 0x6C) = 16;
    MRF32(p + 0x70) = 1.0e10f;
    MRF32(p + 0x74) = -32768.0f;
    MRF32(p + 0x78) = -32768.0f;
    MRF32(p + 0x7C) = 32768.0f;
    MRF32(p + 0x80) = 32768.0f;
    MRF32(p + 0x84) = 0.0f;
    MR32(p + 0x88) = 0x000001FF;
    MR32(p + 0x8C) = 0x00100008;
    MR32(p + 0x90) = 0x03FFFFFF;
    MR32(p + 0x94) = 8;
    MR32(p + 0x98) = 8;
    MR32(p + 0x9C) = 0x0000013B;
    MR32(p + 0xA0) = 8;
    MR32(p + 0xA4) = 6;
    MR32(p + 0xA8) = 4;
    MR32(p + 0xAC) = 8;
    MRF32(p + 0xB0) = 256.0f;
    MR32(p + 0xB4) = 1048575;
    MR32(p + 0xB8) = 1048575;
    MR32(p + 0xBC) = 16;
    MR32(p + 0xC0) = 508;
    /* ProStreet needs vs_3_0 / ps_3_0 (ROADMAP stage S2/S3). */
    MR32(p + 0xC4) = 0xFFFE0300u;            /* VertexShaderVersion */
    MR32(p + 0xC8) = 256;                    /* MaxVertexShaderConst */
    MR32(p + 0xCC) = 0xFFFF0300u;            /* PixelShaderVersion */
    MRF32(p + 0xD0) = 8.0f;
    MR32(p + 0xD4) = 0x00000051;
}

static void fill_displaymode(uint32_t p)
{
    if (!p)
        return;
    MR32(p + 0) = 1280;
    MR32(p + 4) = 720;
    MR32(p + 8) = 60;
    MR32(p + 12) = 22;                       /* D3DFMT_X8R8G8B8 */
}

static void fill_surfdesc(Obj *o, uint32_t p)
{
    if (!p)
        return;
    gzero(p, 32);
    MR32(p + 0) = o->format;
    MR32(p + 4) = (uint32_t)o->type;
    MR32(p + 8) = o->usage;
    MR32(p + 12) = o->pool;
    MR32(p + 16) = 0;                        /* MultiSampleType */
    MR32(p + 20) = 0;
    MR32(p + 24) = o->width;
    MR32(p + 28) = o->height;
}

static uint32_t make_surface(Obj *tex, uint32_t level, uint32_t face,
                             uint32_t format, uint32_t w, uint32_t h,
                             uint32_t data, uint32_t bytes, int type)
{
    uint32_t p = com_new(IF_SURF, 0);
    Obj *s = obj_of(p);
    if (!s)
        return 0;
    s->parent = tex ? tex->guest : 0;
    s->device = g_dev;
    s->width = w;
    s->height = h;
    s->format = format;
    s->type = (uint32_t)type;
    s->usage = 0;
    s->pool = 0;
    s->data = data;
    s->bytes = bytes;
    s->levels = level + 1u;
    s->level_off[level] = 0;
    s->level_bytes[level] = bytes;
    if (tex) {
        s->level_off[0] = face;
    }
    return p;
}

static uint32_t make_shader_or_decl(int iface, uint32_t src)
{
    uint32_t p = com_new(iface, 0);
    Obj *o = obj_of(p);
    if (!o)
        return 0;
    o->data = src;
    o->device = g_dev;
    return p;
}

static uint32_t shader_len(uint32_t src)
{
    if (!src)
        return 0;
    uint32_t n = 0;
    for (; n < 65536u; n += 4u) {
        uint32_t tok = MR32(src + n);
        if (tok == 0x0000FFFFu) {
            n += 4u;
            break;
        }
    }
    return n;
}

static uint32_t decl_len(uint32_t src)
{
    if (!src)
        return 0;
    uint32_t n = 1;
    while (n < 64u) {
        if (MR8(src + n * 8u + 4u) == 0xFFu) {
            n++;
            break;
        }
        n++;
    }
    return n * 8u;
}

static void device_size(uint32_t *w, uint32_t *h)
{
    Obj *d = obj_of(g_dev);
    if (d && d->width && d->height) {
        *w = d->width;
        *h = d->height;
        return;
    }
    *w = 1280;
    *h = 720;
}

static void ensure_backbuffer(uint32_t w, uint32_t h)
{
    if (g_bb) {
        Obj *b = obj_of(g_bb);
        if (b && b->width == w && b->height == h)
            return;
    }
    g_bb = make_surface(0, 0, 0, 22u, w, h, 0, 0, 1);
    Obj *b = obj_of(g_bb);
    if (b)
        b->is_backbuffer = 1;
    if (!g_depth) {
        g_depth = make_surface(0, 0, 0, 75u, w, h, 0, 0, 1);
        Obj *d = obj_of(g_depth);
        if (d)
            d->is_depth = 1;
    }
}

/* ---- D3D9 ---- */

static uint32_t call_d3d9(CPU *c, int m, const char *nm, int nargs)
{
    uint32_t r = 0;
    switch (m) {
    case 0: /* QueryInterface */
        if (MR32(c->esp + 8u)) MR32(MR32(c->esp + 8u)) = MR32(c->esp + 4u);
        break;
    case 1: /* AddRef */
    case 2: { /* Release */
        Obj *o = obj_of(MR32(c->esp + 4u));
        if (o && m == 1) o->refs++;
        if (o && m == 2) o->refs--;
        r = o ? (uint32_t)o->refs : 1;
        break;
    }
    case 4:                                    /* GetAdapterCount */
        r = 1;
        break;
    case 5: {                                  /* GetAdapterIdentifier */
        uint32_t p = MR32(c->esp + 12u);
        if (p) {
            gzero(p, 1100);
            memcpy(G(p), "prostreet-webgpu.dll", 20);
            memcpy(G(p + 512u), "ProStreet WebGPU (bring-up)", 28);
            memcpy(G(p + 1024u), "\\\\.\\DISPLAY1", 12);
            MR32(p + 1056u) = 0x00060000u;
            MR32(p + 1060u) = 0x00000A55u;
            MR32(p + 1064u) = 0x10DEu;         /* NVIDIA */
            MR32(p + 1068u) = 0x0040u;         /* GeForce 6800 */
            MR32(p + 1096u) = 1;
        }
        break;
    }
    case 6:                                    /* GetAdapterModeCount */
        r = 1;
        break;
    case 7: {                                  /* EnumAdapterModes */
        uint32_t p = MR32(c->esp + 16u);
        fill_displaymode(p);
        break;
    }
    case 8:                                    /* GetAdapterDisplayMode */
        fill_displaymode(MR32(c->esp + 12u));
        break;
    case 9:                                    /* CheckDeviceType */
    case 10:                                   /* CheckDeviceFormat */
    case 11: {                                 /* CheckDeviceMultiSampleType */
        uint32_t q = MR32(c->esp + 24u);
        if (q) MR32(q) = 1;
        break;
    }
    case 12:                                   /* CheckDepthStencilMatch */
    case 13:                                   /* CheckDeviceFormatConversion */
        break;
    case 14:                                   /* GetDeviceCaps */
        fill_caps(MR32(c->esp + 16u));
        break;
    case 15:                                   /* GetAdapterMonitor */
        r = 1;
        break;
    case 16: {                                 /* CreateDevice */
        uint32_t pp = MR32(c->esp + 24u);
        uint32_t out = MR32(c->esp + 28u);
        uint32_t w = 1280, h = 720;
        if (pp) {
            if (MR32(pp + 0)) w = MR32(pp + 0);
            if (MR32(pp + 4)) h = MR32(pp + 4);
        }
        if (!g_d3d9) g_d3d9 = MR32(c->esp + 4u);
        if (!g_dev) {
            g_dev = com_new(IF_DEV, 0);
            Obj *d = obj_of(g_dev);
            if (d) { d->width = w; d->height = h; }
        } else {
            Obj *d = obj_of(g_dev);
            if (d) { d->width = w; d->height = h; }
        }
        if (!g_swap) g_swap = com_new(IF_SWAPCHAIN, 0);
        ensure_backbuffer(w, h);
        if (out) MR32(out) = g_dev;
        fprintf(stderr, "[com] CreateDevice %ux%u, device %08x\n", w, h, g_dev);
        break;
    }
    default:
        obj_note(IF_D3D9, nm, c, nargs, 0);
        break;
    }
    return r;
}

/* ---- Device ---- */

static uint32_t call_device(CPU *c, int m, const char *nm, int nargs)
{
    uint32_t r = 0;
    uint32_t self = MR32(c->esp + 4u);
    Obj *dev = obj_of(self);
    switch (m) {
    case 0: /* QueryInterface */
        if (MR32(c->esp + 8u)) MR32(MR32(c->esp + 8u)) = self;
        break;
    case 1: case 2:
        if (dev) { if (m == 1) dev->refs++; else dev->refs--; r = (uint32_t)dev->refs; }
        break;
    case 3: /* TestCooperativeLevel */ break;
    case 4: r = 256u * 1024u * 1024u; break;        /* GetAvailableTextureMem */
    case 5: break;                                   /* EvictManagedResources */
    case 6: if (MR32(c->esp + 8u)) { MR32(MR32(c->esp + 8u)) = g_d3d9; if (obj_of(g_d3d9)) obj_of(g_d3d9)->refs++; } break;
    case 7: fill_caps(MR32(c->esp + 8u)); break;      /* GetDeviceCaps */
    case 8: fill_displaymode(MR32(c->esp + 12u)); break; /* GetDisplayMode */
    case 9: {                                         /* GetCreationParameters */
        uint32_t p = MR32(c->esp + 8u);
        if (p) {
            gzero(p, 16);
            MR32(p + 4) = 1;
            MR32(p + 12) = 0x40;
        }
        break;
    }
    case 10: case 11: break;                          /* cursor */
    case 12: {                                        /* CreateAdditionalSwapChain */
        uint32_t out = MR32(c->esp + 12u);
        if (!g_swap) g_swap = com_new(IF_SWAPCHAIN, 0);
        if (out) MR32(out) = g_swap;
        break;
    }
    case 13:                                          /* GetSwapChain */
        if (MR32(c->esp + 12u)) MR32(MR32(c->esp + 12u)) = g_swap;
        break;
    case 14: r = 1; break;                            /* GetNumberOfSwapChains */
    case 15: {                                        /* Reset */
        uint32_t pp = MR32(c->esp + 8u);
        uint32_t w = 1280, h = 720;
        if (pp) {
            if (MR32(pp + 0)) w = MR32(pp + 0);
            if (MR32(pp + 4)) h = MR32(pp + 4);
        }
        if (dev) { dev->width = w; dev->height = h; }
        ensure_backbuffer(w, h);
        fprintf(stderr, "[com] Reset %ux%u\n", w, h);
        break;
    }
    case 16:                                          /* Present */
        g_presented++;
        gpu_present();
        break;
    case 17: {                                        /* GetBackBuffer */
        uint32_t out = MR32(c->esp + 20u);
        uint32_t w, h;
        device_size(&w, &h);
        ensure_backbuffer(w, h);
        if (out) MR32(out) = g_bb;
        break;
    }
    case 18: {                                        /* GetRasterStatus */
        uint32_t p = MR32(c->esp + 12u);
        if (p) { MR32(p) = 0; MR32(p + 4) = 0; MR32(p + 8) = 0; }
        break;
    }
    case 19: break;                                   /* SetDialogBoxMode */
    case 20: case 21: break;                          /* gamma */
    case 22: {                                        /* CreateTexture */
        uint32_t w = MR32(c->esp + 8u), h = MR32(c->esp + 12u);
        uint32_t lv = MR32(c->esp + 16u), fmt = MR32(c->esp + 24u);
        uint32_t out = MR32(c->esp + 32u);
        uint32_t p = com_new(IF_TEX, 0);
        Obj *o = obj_of(p);
        if (o) {
            o->device = g_dev;
            o->type = 3;
            o->usage = MR32(c->esp + 20u);
            o->pool = MR32(c->esp + 28u);
            if (!tex_layout(o, w, h, lv, fmt, 1u)) {
                g_obj_n--;
                p = 0;
            }
        }
        if (out) MR32(out) = p;
        break;
    }
    case 23: {                                        /* CreateVolumeTexture */
        uint32_t w = MR32(c->esp + 8u), h = MR32(c->esp + 12u);
        uint32_t fmt = MR32(c->esp + 28u);
        uint32_t out = MR32(c->esp + 36u);
        uint32_t p = com_new(IF_VOLTEX, 0);
        Obj *o = obj_of(p);
        if (o) {
            o->device = g_dev;
            o->type = 4;
            if (!tex_layout(o, w, h, 1, fmt, 1u)) {
                g_obj_n--;
                p = 0;
            }
        }
        if (out) MR32(out) = p;
        break;
    }
    case 24: {                                        /* CreateCubeTexture */
        uint32_t edge = MR32(c->esp + 8u), lv = MR32(c->esp + 12u);
        uint32_t fmt = MR32(c->esp + 20u);
        uint32_t out = MR32(c->esp + 28u);
        uint32_t p = com_new(IF_CUBETEX, 0);
        Obj *o = obj_of(p);
        if (o) {
            o->device = g_dev;
            o->type = 5;
            o->usage = MR32(c->esp + 16u);
            o->pool = MR32(c->esp + 24u);
            if (!tex_layout(o, edge, edge, lv, fmt, 6u)) {
                g_obj_n--;
                p = 0;
            }
        }
        if (out) MR32(out) = p;
        break;
    }
    case 25: {                                        /* CreateVertexBuffer */
        uint32_t len = MR32(c->esp + 8u), fvf = MR32(c->esp + 16u);
        uint32_t out = MR32(c->esp + 24u);
        uint32_t p = com_new(IF_VB, 0);
        Obj *o = obj_of(p);
        if (o) {
            o->device = g_dev;
            o->type = 6;
            o->usage = MR32(c->esp + 12u);
            o->fvf = fvf;
            o->pool = MR32(c->esp + 20u);
            o->size = len;
            o->bytes = len;
            o->data = com_alloc(len ? len : 1u);
            if (!o->data) { g_obj_n--; p = 0; }
        }
        if (out) MR32(out) = p;
        break;
    }
    case 26: {                                        /* CreateIndexBuffer */
        uint32_t len = MR32(c->esp + 8u), fmt = MR32(c->esp + 16u);
        uint32_t out = MR32(c->esp + 24u);
        uint32_t p = com_new(IF_IB, 0);
        Obj *o = obj_of(p);
        if (o) {
            o->device = g_dev;
            o->type = 7;
            o->usage = MR32(c->esp + 12u);
            o->format = fmt;
            o->pool = MR32(c->esp + 20u);
            o->size = len;
            o->bytes = len;
            o->data = com_alloc(len ? len : 1u);
            if (!o->data) { g_obj_n--; p = 0; }
        }
        if (out) MR32(out) = p;
        break;
    }
    case 27: case 28: {                               /* CreateRenderTarget / DepthStencil */
        uint32_t w = MR32(c->esp + 8u), h = MR32(c->esp + 12u);
        uint32_t fmt = MR32(c->esp + 16u);
        uint32_t out = MR32(c->esp + 32u);
        uint32_t p = make_surface(0, 0, 0, fmt, w, h, 0, 0, 1);
        Obj *o = obj_of(p);
        if (o) {
            o->device = g_dev;
            o->usage = 0x1;                           /* render target */
            o->bytes = tex_level_size(fmt, w, h);
            o->bytes = (o->bytes + 3u) & ~3u;
            o->data = com_alloc(o->bytes);
        }
        if (out) MR32(out) = p;
        break;
    }
    case 29: case 30: break;                          /* UpdateSurface/UpdateTexture */
    case 31: case 32: case 33: case 34: break;        /* GetRenderTargetData/FrontBuffer/StretchRect/ColorFill */
    case 35: {                                        /* CreateOffscreenPlainSurface */
        uint32_t w = MR32(c->esp + 8u), h = MR32(c->esp + 12u);
        uint32_t fmt = MR32(c->esp + 16u), pool = MR32(c->esp + 20u);
        uint32_t out = MR32(c->esp + 24u);
        uint32_t bytes = tex_level_size(fmt, w, h);
        bytes = (bytes + 3u) & ~3u;
        uint32_t p = make_surface(0, 0, 0, fmt, w, h, 0, bytes, 1);
        Obj *o = obj_of(p);
        if (o) { o->device = g_dev; o->pool = pool; o->data = com_alloc(bytes); }
        if (out) MR32(out) = p;
        break;
    }
    case 36: {                                        /* SetRenderTarget */
        uint32_t idx = MR32(c->esp + 8u), rt = MR32(c->esp + 12u);
        if (dev && idx < 4) dev->rt[idx] = rt;
        break;
    }
    case 37: {                                        /* GetRenderTarget */
        uint32_t idx = MR32(c->esp + 8u), out = MR32(c->esp + 12u);
        if (out) MR32(out) = (dev && idx < 4 && dev->rt[idx]) ? dev->rt[idx] : g_bb;
        break;
    }
    case 38:                                          /* SetDepthStencilSurface */
        if (dev) dev->depth = MR32(c->esp + 8u);
        break;
    case 39:                                          /* GetDepthStencilSurface */
        if (MR32(c->esp + 8u)) MR32(MR32(c->esp + 8u)) = (dev && dev->depth) ? dev->depth : g_depth;
        break;
    case 40: case 41: break;                          /* BeginScene/EndScene */
    case 42: {                                        /* Clear */
        uint32_t color = MR32(c->esp + 20u);
        if (MR32(c->esp + 16u) & 1u) {                /* D3DCLEAR_TARGET */
            gpu_clear(color);
            g_cleared++;
        }
        break;
    }
    case 43: case 44: case 45: break;                 /* transform */
    case 46: case 47: case 48: break;                 /* viewport */
    case 49: {                                        /* SetMaterial */
        (void)0;
        break;
    }
    case 50: {                                        /* GetMaterial */
        uint32_t p = MR32(c->esp + 8u);
        if (p) gzero(p, 68);
        break;
    }
    case 51: case 52: case 53: case 54: break;        /* lights */
    case 55: case 56: break;                          /* clip plane */
    case 57: break;                                   /* SetRenderState */
    case 58: {                                        /* GetRenderState */
        uint32_t out = MR32(c->esp + 12u);
        if (out) MR32(out) = 0;
        break;
    }
    case 59: {                                        /* CreateStateBlock */
        uint32_t out = MR32(c->esp + 12u);
        uint32_t p = com_new(IF_STATEBLOCK, 0);
        obj_of(p)->device = g_dev;
        if (out) MR32(out) = p;
        break;
    }
    case 60: break;                                   /* BeginStateBlock */
    case 61: {                                        /* EndStateBlock */
        uint32_t out = MR32(c->esp + 8u);
        uint32_t p = com_new(IF_STATEBLOCK, 0);
        obj_of(p)->device = g_dev;
        if (out) MR32(out) = p;
        break;
    }
    case 62: case 63: break;                          /* clip status */
    case 64: {                                        /* GetTexture */
        uint32_t stage = MR32(c->esp + 8u), out = MR32(c->esp + 12u);
        if (out) MR32(out) = (dev && stage < 16) ? dev->tex[stage] : 0;
        break;
    }
    case 65: {                                        /* SetTexture */
        uint32_t stage = MR32(c->esp + 8u), tex = MR32(c->esp + 12u);
        if (dev && stage < 16) dev->tex[stage] = tex;
        break;
    }
    case 66: case 67: case 68: case 69: break;        /* texture stage / sampler */
    case 70: {                                        /* ValidateDevice */
        uint32_t out = MR32(c->esp + 8u);
        if (out) MR32(out) = 1;
        break;
    }
    case 71: case 72: case 73: case 74: break;        /* palette */
    case 75: case 76: break;                          /* scissor */
    case 77: break;                                   /* SetSoftwareVertexProcessing */
    case 78: r = 0; break;                            /* GetSoftwareVertexProcessing */
    case 79: case 80: break;                          /* NPatch */
    case 81:                                          /* DrawPrimitive */
    case 82:                                          /* DrawIndexedPrimitive */
    case 83:                                          /* DrawPrimitiveUP */
    case 84:                                          /* DrawIndexedPrimitiveUP */
        g_draws++;
        break;
    case 85: break;                                   /* ProcessVertices */
    case 86: {                                        /* CreateVertexDeclaration */
        uint32_t src = MR32(c->esp + 8u), out = MR32(c->esp + 12u);
        uint32_t p = make_shader_or_decl(IF_VDECL, src);
        Obj *o = obj_of(p);
        if (o) o->len = decl_len(src);
        if (out) MR32(out) = p;
        break;
    }
    case 87:                                          /* SetVertexDeclaration */
        if (dev) dev->decl = MR32(c->esp + 8u);
        break;
    case 88: {                                        /* GetVertexDeclaration */
        uint32_t out = MR32(c->esp + 8u);
        if (out) MR32(out) = dev ? dev->decl : 0;
        break;
    }
    case 89:                                          /* SetFVF */
        if (dev) dev->fvf = MR32(c->esp + 8u);
        break;
    case 90: {                                        /* GetFVF */
        uint32_t out = MR32(c->esp + 8u);
        if (out) MR32(out) = dev ? dev->fvf : 0;
        break;
    }
    case 91: {                                        /* CreateVertexShader */
        uint32_t src = MR32(c->esp + 8u), out = MR32(c->esp + 12u);
        uint32_t p = make_shader_or_decl(IF_VSHADER, src);
        Obj *o = obj_of(p);
        if (o) o->len = shader_len(src);
        if (out) MR32(out) = p;
        fprintf(stderr, "[com] CreateVertexShader %08x (%u bytes) -> %08x\n",
                src, o ? o->len : 0u, p);
        break;
    }
    case 92:                                          /* SetVertexShader */
        if (dev) dev->vs = MR32(c->esp + 8u);
        break;
    case 93: {                                        /* GetVertexShader */
        uint32_t out = MR32(c->esp + 8u);
        if (out) MR32(out) = dev ? dev->vs : 0;
        break;
    }
    case 94: case 95: case 96: case 97: case 98: case 99: break; /* constants */
    case 100: {                                       /* SetStreamSource */
        uint32_t n = MR32(c->esp + 8u);
        if (dev && n < 16) {
            dev->streams[n] = MR32(c->esp + 12u);
            dev->stream_off[n] = MR32(c->esp + 16u);
            dev->stream_stride[n] = MR32(c->esp + 20u);
        }
        break;
    }
    case 101: {                                       /* GetStreamSource */
        uint32_t n = MR32(c->esp + 8u), out = MR32(c->esp + 12u);
        if (out) MR32(out) = (dev && n < 16) ? dev->streams[n] : 0;
        if (dev && n < 16) {
            if (MR32(c->esp + 16u)) MR32(MR32(c->esp + 16u)) = dev->stream_off[n];
            if (MR32(c->esp + 20u)) MR32(MR32(c->esp + 20u)) = dev->stream_stride[n];
        }
        break;
    }
    case 102: case 103: break;                        /* stream freq */
    case 104:                                         /* SetIndices */
        if (dev) dev->indices = MR32(c->esp + 8u);
        break;
    case 105: {                                       /* GetIndices */
        uint32_t out = MR32(c->esp + 8u);
        if (out) MR32(out) = dev ? dev->indices : 0;
        break;
    }
    case 106: {                                       /* CreatePixelShader */
        uint32_t src = MR32(c->esp + 8u), out = MR32(c->esp + 12u);
        uint32_t p = make_shader_or_decl(IF_SIMPLE, src);
        Obj *o = obj_of(p);
        if (o) o->len = shader_len(src);
        if (out) MR32(out) = p;
        fprintf(stderr, "[com] CreatePixelShader %08x (%u bytes) -> %08x\n",
                src, o ? o->len : 0u, p);
        break;
    }
    case 107:                                         /* SetPixelShader */
        if (dev) dev->ps = MR32(c->esp + 8u);
        break;
    case 108: {                                       /* GetPixelShader */
        uint32_t out = MR32(c->esp + 8u);
        if (out) MR32(out) = dev ? dev->ps : 0;
        break;
    }
    case 109: case 110: case 111: case 112: case 113: case 114: break; /* constants */
    case 115: case 116: case 117: break;              /* patches */
    case 118: {                                       /* CreateQuery */
        uint32_t out = MR32(c->esp + 12u);
        uint32_t p = com_new(IF_QUERY, 0);
        obj_of(p)->device = g_dev;
        if (out) MR32(out) = p;
        break;
    }
    default:
        obj_note(IF_DEV, nm, c, nargs, 0);
        break;
    }
    return r;
}

/* ---- resources ---- */

static uint32_t call_texture(CPU *c, int iface, int m, const char *nm, int nargs)
{
    uint32_t r = 0;
    uint32_t self = MR32(c->esp + 4u);
    Obj *o = obj_of(self);
    int cube = (iface == IF_CUBETEX);
    switch (m) {
    case 0: if (MR32(c->esp + 8u)) MR32(MR32(c->esp + 8u)) = self; break;
    case 1: case 2:
        if (o) { if (m == 1) o->refs++; else o->refs--; r = (uint32_t)o->refs; }
        break;
    case 3: if (MR32(c->esp + 8u)) MR32(MR32(c->esp + 8u)) = o ? o->device : g_dev; break;
    case 4: case 5: case 6: case 7: case 8: case 9: break; /* private/priority */
    case 10: r = o ? o->type : 3; break;              /* GetType */
    case 11: case 12: break;                         /* LOD */
    case 13: r = o ? o->levels : 1; break;           /* GetLevelCount */
    case 14: case 15: case 16: break;                /* autogen */
    case 17: {                                       /* GetLevelDesc */
        uint32_t lv = MR32(c->esp + 8u), p = MR32(c->esp + 12u);
        if (o && p) {
            Obj tmp = *o;
            tmp.width = o->width >> lv; if (!tmp.width) tmp.width = 1;
            tmp.height = o->height >> lv; if (!tmp.height) tmp.height = 1;
            fill_surfdesc(&tmp, p);
        } else if (p) gzero(p, 32);
        break;
    }
    case 18: {                                       /* GetSurfaceLevel / GetCubeMapSurface */
        uint32_t a1 = MR32(c->esp + 8u), a2 = MR32(c->esp + 12u);
        uint32_t a3 = MR32(c->esp + 16u);
        uint32_t level = cube ? a2 : a1;
        uint32_t face = cube ? a1 : 0;
        uint32_t out = cube ? a3 : a2;
        if (o && level < MAX_LEVELS && level < o->levels) {
            uint32_t w = o->width >> level; if (!w) w = 1;
            uint32_t h = o->height >> level; if (!h) h = 1;
            uint32_t base = o->data + (uint32_t)face * o->level_total + o->level_off[level];
            uint32_t p = make_surface(o, level, face, o->format, w, h,
                                      base, o->level_bytes[level], 1);
            if (out) MR32(out) = p;
        } else if (out) {
            MR32(out) = 0;
        }
        break;
    }
    case 19: {                                       /* LockRect */
        uint32_t a1 = MR32(c->esp + 8u), a2 = MR32(c->esp + 12u);
        uint32_t a3 = MR32(c->esp + 16u);
        uint32_t level = cube ? a2 : a1;
        uint32_t face = cube ? a1 : 0;
        uint32_t lr = cube ? a3 : a2;
        if (o && lr) {
            if (level >= o->levels) level = 0;
            uint32_t w = o->width >> level; if (!w) w = 1;
            uint32_t base = o->data + (uint32_t)face * o->level_total + o->level_off[level];
            MR32(lr) = fmt_rowbytes(o->format, w);
            MR32(lr + 4u) = base;
            o->lock_ptr = base;
        }
        break;
    }
    case 20: break;                                  /* UnlockRect */
    case 21: break;                                  /* AddDirtyRect */
    default:
        obj_note(iface, nm, c, nargs, 0);
        break;
    }
    return r;
}

static uint32_t call_surface(CPU *c, int m, const char *nm, int nargs)
{
    uint32_t r = 0;
    uint32_t self = MR32(c->esp + 4u);
    Obj *o = obj_of(self);
    switch (m) {
    case 0: if (MR32(c->esp + 8u)) MR32(MR32(c->esp + 8u)) = self; break;
    case 1: case 2:
        if (o) { if (m == 1) o->refs++; else o->refs--; r = (uint32_t)o->refs; }
        break;
    case 3: if (MR32(c->esp + 8u)) MR32(MR32(c->esp + 8u)) = o ? o->device : g_dev; break;
    case 4: case 5: case 6: case 7: case 8: case 9: break;
    case 10: r = 1; break;                           /* GetType = SURFACE */
    case 11: {                                       /* GetContainer */
        uint32_t out = MR32(c->esp + 12u);
        if (out) MR32(out) = o ? o->parent : 0;
        break;
    }
    case 12:                                         /* GetDesc */
        if (o) fill_surfdesc(o, MR32(c->esp + 8u));
        break;
    case 13: {                                       /* LockRect */
        uint32_t lr = MR32(c->esp + 8u);
        if (o && lr) {
            MR32(lr) = fmt_rowbytes(o->format, o->width);
            MR32(lr + 4u) = o->data;
            o->lock_ptr = o->data;
        }
        break;
    }
    case 14: break;                                  /* UnlockRect */
    case 15: {                                       /* GetDC */
        uint32_t out = MR32(c->esp + 8u);
        if (out) MR32(out) = o ? o->data : 0;
        break;
    }
    case 16: break;                                  /* ReleaseDC */
    default: obj_note(IF_SURF, nm, c, nargs, 0); break;
    }
    return r;
}

static uint32_t call_buffer(CPU *c, int iface, int m, const char *nm, int nargs)
{
    uint32_t r = 0;
    uint32_t self = MR32(c->esp + 4u);
    Obj *o = obj_of(self);
    switch (m) {
    case 0: if (MR32(c->esp + 8u)) MR32(MR32(c->esp + 8u)) = self; break;
    case 1: case 2:
        if (o) { if (m == 1) o->refs++; else o->refs--; r = (uint32_t)o->refs; }
        break;
    case 3: if (MR32(c->esp + 8u)) MR32(MR32(c->esp + 8u)) = o ? o->device : g_dev; break;
    case 4: case 5: case 6: case 7: case 8: case 9: break;
    case 10: r = o ? o->type : (uint32_t)(iface == IF_VB ? 6 : 7); break;
    case 11: {                                       /* Lock */
        uint32_t off = MR32(c->esp + 8u), size = MR32(c->esp + 12u);
        uint32_t out = MR32(c->esp + 16u);
        if (o && out) {
            if (off > o->size) off = o->size;
            if (!size || off + size > o->size) size = o->size - off;
            MR32(out) = o->data + off;
            o->lock_ptr = o->data + off;
        }
        break;
    }
    case 12: break;                                  /* Unlock */
    case 13: {                                       /* GetDesc */
        uint32_t p = MR32(c->esp + 8u);
        if (p && o) {
            gzero(p, 24);
            MR32(p + 0) = o->format;
            MR32(p + 4) = (uint32_t)o->type;
            MR32(p + 8) = o->usage;
            MR32(p + 12) = o->pool;
            MR32(p + 16) = o->size;
            if (iface == IF_VB) MR32(p + 20) = o->fvf;
        }
        break;
    }
    default: obj_note(iface, nm, c, nargs, 0); break;
    }
    return r;
}

static uint32_t call_swapchain(CPU *c, int m, const char *nm, int nargs)
{
    uint32_t r = 0;
    switch (m) {
    case 0: if (MR32(c->esp + 8u)) MR32(MR32(c->esp + 8u)) = MR32(c->esp + 4u); break;
    case 1: case 2: r = 1; break;                    /* AddRef/Release */
    case 3: g_presented++; gpu_present(); break;      /* Present */
    case 4: break;                                   /* GetFrontBufferData */
    case 5: {                                        /* GetBackBuffer */
        uint32_t out = MR32(c->esp + 16u);
        uint32_t w, h;
        device_size(&w, &h);
        ensure_backbuffer(w, h);
        if (out) MR32(out) = g_bb;
        break;
    }
    case 6: {                                        /* GetRasterStatus */
        uint32_t p = MR32(c->esp + 8u);
        if (p) { MR32(p) = 0; MR32(p + 4) = 0; MR32(p + 8) = 0; }
        break;
    }
    case 7: fill_displaymode(MR32(c->esp + 8u)); break;
    case 8: if (MR32(c->esp + 8u)) MR32(MR32(c->esp + 8u)) = g_dev; break;
    case 9: {                                        /* GetPresentParameters */
        uint32_t p = MR32(c->esp + 8u);
        if (p) {
            uint32_t w, h;
            device_size(&w, &h);
            gzero(p, 56);
            MR32(p + 0) = w; MR32(p + 4) = h; MR32(p + 8) = 22;
            MR32(p + 12) = 1; MR32(p + 24) = 1; MR32(p + 32) = 1;
            MR32(p + 52) = 1;
        }
        break;
    }
    default: obj_note(IF_SWAPCHAIN, nm, c, nargs, 0); break;
    }
    return r;
}

static uint32_t call_simple(CPU *c, int iface, int m, const char *nm, int nargs)
{
    uint32_t r = 0;
    uint32_t self = MR32(c->esp + 4u);
    Obj *o = obj_of(self);
    switch (m) {
    case 0: if (MR32(c->esp + 8u)) MR32(MR32(c->esp + 8u)) = self; break;
    case 1: case 2:
        if (o) { if (m == 1) o->refs++; else o->refs--; r = (uint32_t)o->refs; }
        break;
    case 3: if (MR32(c->esp + 8u)) MR32(MR32(c->esp + 8u)) = o ? o->device : g_dev; break;
    case 4: {                                        /* GetFunction / GetDeclaration */
        uint32_t pdata = MR32(c->esp + 8u), psize = MR32(c->esp + 12u);
        uint32_t sz = o ? o->len : 0;
        if (pdata && o && o->data && sz) memcpy(G(pdata), G(o->data), sz);
        if (psize) MR32(psize) = sz;
        break;
    }
    case 5: {                                        /* Capture / Apply */
        break;
    }
    default: obj_note(iface, nm, c, nargs, 0); break;
    }
    return r;
}

static uint32_t call_query(CPU *c, int m, const char *nm, int nargs)
{
    uint32_t r = 0;
    switch (m) {
    case 0: if (MR32(c->esp + 8u)) MR32(MR32(c->esp + 8u)) = MR32(c->esp + 4u); break;
    case 1: case 2: r = 1; break;
    case 3: if (MR32(c->esp + 8u)) MR32(MR32(c->esp + 8u)) = g_dev; break;
    case 4: r = 0; break;                            /* GetType */
    case 5: r = 0; break;                            /* GetDataSize */
    case 6: break;                                   /* Issue */
    case 7: {                                        /* GetData */
        uint32_t out = MR32(c->esp + 16u);
        if (out) MR32(out) = 0;
        break;
    }
    default: obj_note(IF_QUERY, nm, c, nargs, 0); break;
    }
    return r;
}

/* Called by dispatch_indirect() for a 0xE0000000.. magic. */
int com_call(CPU *c, uint32_t magic)
{
    uint32_t iface = (magic >> 16) & 0x1FFFu;
    uint32_t m = magic & 0xFFFFu;
    POP32(c->eip);
    if (iface == 0 || iface >= NEL(g_if) || m >= g_if[iface].n) {
        fprintf(stderr, "[com] invalid interface/method %u:%u from eip %08x\n",
                iface, m, c->eip);
        c->eax = 0x80004001u;
        return 1;
    }
    const Method *M = &g_if[iface].m[m];
    /* Cube textures need the leading FaceType in the pop count. */
    int nargs = M->nargs;
    if (iface == IF_CUBETEX &&
        (m == 18 || m == 19))              /* GetCubeMapSurface, LockRect */
        nargs++;
    uint32_t r;
    switch (iface) {
    case IF_D3D9:     r = call_d3d9(c, (int)m, M->name, nargs); break;
    case IF_DEV:      r = call_device(c, (int)m, M->name, nargs); break;
    case IF_TEX:
    case IF_CUBETEX:
    case IF_VOLTEX:   r = call_texture(c, (int)iface, (int)m, M->name, nargs); break;
    case IF_VB:
    case IF_IB:       r = call_buffer(c, (int)iface, (int)m, M->name, nargs); break;
    case IF_SURF:     r = call_surface(c, (int)m, M->name, nargs); break;
    case IF_SWAPCHAIN:r = call_swapchain(c, (int)m, M->name, nargs); break;
    case IF_QUERY:    r = call_query(c, (int)m, M->name, nargs); break;
    default:          r = call_simple(c, (int)iface, (int)m, M->name, nargs); break;
    }
    c->eax = r;
    c->esp += 4u * (uint32_t)nargs;
    return 0;
}

/* Direct3DCreate9(SDKVersion) */
uint32_t com_direct3dcreate9(void)
{
    if (!g_d3d9) {
        g_d3d9 = com_new(IF_D3D9, 0);
        fprintf(stderr, "[com] Direct3DCreate9 -> IDirect3D9 %08x\n", g_d3d9);
    } else {
        Obj *o = obj_of(g_d3d9);
        if (o) o->refs++;
    }
    return g_d3d9;
}

/* Reported by the page alongside the import histogram. */
unsigned com_presented(void) { return g_presented; }
unsigned com_cleared(void)   { return g_cleared; }
unsigned com_draws(void)     { return g_draws; }

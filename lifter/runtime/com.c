/* COM bridge for D3D9 / DirectInput8
 * Provides guest COM objects and dispatches their vtable calls.
 */
#include "rt.h"
#include "cpu.h"
#include "d3d9_tables.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define COM_MAGIC 0xE0000000u
#define COM_BASE  0x38000000u
#define COM_MAX   0x3C000000u

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

static uint32_t com_alloc(uint32_t n) {
    n = (n + 15u) & ~15u;
    if (g_com_ptr + n > COM_MAX) return 0;
    uint32_t p = g_com_ptr;
    g_com_ptr += n;
    mem_commit(p, n);
    memset(g_mem + p, 0, n);
    return p;
}

uint32_t com_new(uint32_t iface, uint32_t extra) {
    if (iface == 0 || iface >= NEL(g_if)) return 0;
    unsigned n = g_if[iface].n;
    uint32_t vt = g_vtab[iface];
    if (!vt) {
        vt = com_alloc(n * 4u);
        for (unsigned i = 0; i < n; i++) {
            MR32(vt + 4u * i) = COM_MAGIC | (iface << 16) | i;
        }
        g_vtab[iface] = vt;
    }
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

uint32_t com_directinput8create(uint32_t ppvOut) {
    uint32_t di8 = com_new(IF_DI8, 64);
    if (ppvOut && mem_is_committed(ppvOut)) {
        MR32(ppvOut) = di8;
    }
    fprintf(stderr, "com: IDirectInput8 created 0x%08x\n", di8);
    return 0; /* DI_OK */
}

#define COM_A(i) MR32(c->esp + 4u * (uint32_t)(i))

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
        r = 1;
    } else if (m == 2) { /* Release(this) */
        r = 0;
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
            uint32_t pp = COM_A(7);
            if (pp && mem_is_committed(pp)) MR32(pp) = com_new(IF_TEX, 64);
            r = 0;
        } else if (!strcmp(nm, "CreateCubeTexture")) {
            uint32_t pp = COM_A(6);
            if (pp && mem_is_committed(pp)) MR32(pp) = com_new(IF_CUBETEX, 64);
            r = 0;
        } else if (!strcmp(nm, "CreateVertexBuffer")) {
            uint32_t len = COM_A(1);
            if (len < 65536) len = 65536;
            uint32_t buf = com_alloc(len);
            uint32_t obj = com_new(IF_VB, 64);
            MR32(obj + 4) = buf;
            MR32(obj + 8) = len;
            uint32_t pp = COM_A(5);
            if (pp && mem_is_committed(pp)) MR32(pp) = obj;
            r = 0;
        } else if (!strcmp(nm, "CreateIndexBuffer")) {
            uint32_t len = COM_A(1);
            if (len < 65536) len = 65536;
            uint32_t buf = com_alloc(len);
            uint32_t obj = com_new(IF_IB, 64);
            MR32(obj + 4) = buf;
            MR32(obj + 8) = len;
            uint32_t pp = COM_A(5);
            if (pp && mem_is_committed(pp)) MR32(pp) = obj;
            r = 0;
        } else if (!strcmp(nm, "CreateRenderTarget") || !strcmp(nm, "CreateDepthStencilSurface")) {
            uint32_t pp = COM_A(7);
            if (pp && mem_is_committed(pp)) MR32(pp) = com_new(IF_SURF, 64);
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
            uint32_t pp = COM_A(2);
            if (pp && mem_is_committed(pp)) MR32(pp) = com_new(IF_VDECL, 64);
            r = 0;
        } else if (!strcmp(nm, "CreateVertexShader") || !strcmp(nm, "CreatePixelShader")) {
            uint32_t pp = COM_A(2);
            if (pp && mem_is_committed(pp)) MR32(pp) = com_new(IF_VSHADER, 64);
            r = 0;
        } else if (!strcmp(nm, "GetDirect3D")) {
            uint32_t pp = COM_A(1);
            if (pp && mem_is_committed(pp)) MR32(pp) = g_d3d9;
            r = 0;
        } else {
            r = 0;
        }
    } else if (iface == IF_TEX) {
        if (!strcmp(nm, "GetSurfaceLevel")) {
            uint32_t pp = COM_A(2);
            if (pp && mem_is_committed(pp)) MR32(pp) = com_new(IF_SURF, 64);
            r = 0;
        } else if (!strcmp(nm, "GetLevelDesc")) {
            uint32_t pd = COM_A(2);
            if (pd && mem_is_committed(pd)) {
                mem_commit(pd, 32);
                MR32(pd + 0) = 21; /* D3DFMT_A8R8G8B8 */
                MR32(pd + 4) = 1;  /* D3DRTYPE_SURFACE */
                MR32(pd + 8) = 0;  /* Usage */
                MR32(pd + 12) = 0; /* D3DPOOL_DEFAULT */
                MR32(pd + 16) = 0; /* D3DMULTISAMPLE_NONE */
                MR32(pd + 20) = 0; /* Quality */
                MR32(pd + 24) = 1; /* Width */
                MR32(pd + 28) = 1; /* Height */
            }
            r = 0;
        } else if (!strcmp(nm, "LockRect")) {
            uint32_t plr = COM_A(2);
            if (plr && mem_is_committed(plr)) {
                mem_commit(plr, 8);
                uint32_t buf = com_alloc(1280 * 720 * 4);
                MR32(plr + 0) = 1280 * 4;
                MR32(plr + 4) = buf;
            }
            r = 0;
        } else {
            r = 0;
        }
    } else if (iface == IF_CUBETEX) {
        if (!strcmp(nm, "GetCubeMapSurface")) {
            uint32_t pp = COM_A(3);
            if (pp && mem_is_committed(pp)) MR32(pp) = com_new(IF_SURF, 64);
            r = 0;
        } else if (!strcmp(nm, "GetLevelDesc")) {
            uint32_t pd = COM_A(2);
            if (pd && mem_is_committed(pd)) {
                mem_commit(pd, 32);
                MR32(pd + 0) = 21; /* D3DFMT_A8R8G8B8 */
                MR32(pd + 4) = 1;  /* D3DRTYPE_SURFACE */
                MR32(pd + 8) = 0;  /* Usage */
                MR32(pd + 12) = 0; /* D3DPOOL_DEFAULT */
                MR32(pd + 16) = 0; /* D3DMULTISAMPLE_NONE */
                MR32(pd + 20) = 0; /* Quality */
                MR32(pd + 24) = 1; /* Width */
                MR32(pd + 28) = 1; /* Height */
            }
            r = 0;
        } else if (!strcmp(nm, "LockRect")) {
            uint32_t plr = COM_A(2);
            if (plr && mem_is_committed(plr)) {
                mem_commit(plr, 8);
                uint32_t buf = com_alloc(256 * 256 * 4);
                MR32(plr + 0) = 256 * 4;
                MR32(plr + 4) = buf;
            }
            r = 0;
        } else {
            r = 0;
        }
    } else if (iface == IF_SURF) {
        if (!strcmp(nm, "GetDesc")) {
            uint32_t pd = COM_A(1);
            if (pd && mem_is_committed(pd)) {
                mem_commit(pd, 32);
                MR32(pd + 0) = 22; /* D3DFMT_X8R8G8B8 */
                MR32(pd + 4) = 1;  /* D3DRTYPE_SURFACE */
                MR32(pd + 8) = 1;  /* D3DUSAGE_RENDERTARGET */
                MR32(pd + 12) = 0; /* D3DPOOL_DEFAULT */
                MR32(pd + 16) = 0; /* D3DMULTISAMPLE_NONE */
                MR32(pd + 20) = 0; /* Quality */
                MR32(pd + 24) = 1280; /* Width */
                MR32(pd + 28) = 720;  /* Height */
            }
            r = 0;
        } else if (!strcmp(nm, "LockRect")) {
            uint32_t plr = COM_A(1);
            if (plr && mem_is_committed(plr)) {
                mem_commit(plr, 8);
                uint32_t buf = com_alloc(1280 * 720 * 4);
                MR32(plr + 0) = 1280 * 4;
                MR32(plr + 4) = buf;
            }
            r = 0;
        } else {
            r = 0;
        }
    } else if (iface == IF_VB || iface == IF_IB) {
        if (!strcmp(nm, "Lock")) {
            uint32_t this_obj = COM_A(0);
            uint32_t offset = COM_A(1);
            uint32_t ppb = COM_A(3);
            uint32_t buf = MR32(this_obj + 4);
            if (!buf) {
                buf = com_alloc(0x40000);
                MR32(this_obj + 4) = buf;
            }
            if (ppb && mem_is_committed(ppb)) {
                MR32(ppb) = buf + offset;
            }
            r = 0;
        } else if (!strcmp(nm, "Unlock")) {
            r = 0;
        } else if (!strcmp(nm, "GetDesc")) {
            uint32_t pd = COM_A(1);
            if (pd && mem_is_committed(pd)) {
                mem_commit(pd, 24);
                MR32(pd + 0) = (iface == IF_IB) ? 101 : 0; /* D3DFMT_INDEX16 */
                MR32(pd + 4) = (iface == IF_IB) ? 5 : 4;   /* Type: D3DRTYPE_INDEXBUFFER / VERTEXBUFFER */
                MR32(pd + 8) = 0;
                MR32(pd + 12) = 0;
                MR32(pd + 16) = MR32(COM_A(0) + 8);
            }
            r = 0;
        } else {
            r = 0;
        }
    } else if (iface == IF_DI8) {
        if (!strcmp(nm, "CreateDevice")) {
            uint32_t pp = COM_A(2);
            if (pp && mem_is_committed(pp)) MR32(pp) = com_new(IF_DIDEV, 64);
            r = 0;
        } else {
            r = 0;
        }
    } else if (iface == IF_DIDEV) {
        if (!strcmp(nm, "GetDeviceState")) {
            uint32_t sz = COM_A(1);
            uint32_t p = COM_A(2);
            if (p) {
                mem_commit(p, sz);
                memset(g_mem + p, 0, sz);
            }
            r = 0;
        } else if (!strcmp(nm, "GetDeviceData")) {
            uint32_t pq = COM_A(2);
            if (pq && mem_is_committed(pq)) MR32(pq) = 0;
            r = 0;
        } else {
            r = 0;
        }
    } else if (iface == IF_EFFECT) {
        if (!strcmp(nm, "GetDesc") || m == 3) {
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
        } else if (!strcmp(nm, "SetRawValue") || m == 78) {
            r = 0;
        } else {
            r = 0;
        }
    } else {
        r = 0;
    }

    c->eax = r;
    c->esp += 4u * (uint32_t)nargs;
    return 1;
}

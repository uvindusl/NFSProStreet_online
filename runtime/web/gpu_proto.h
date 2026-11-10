/* ProStreet GPU protocol — shared between runtime/web/gpu_web.c and
 * web/src/gpu (TS).  Defines the Gw* types, the recording API, and the
 * page-side bindings (imports from the page's nfsu2 module).
 *
 * The game records draw calls into a shared Buf; the page's host process
 * turns them into WebGPU commands.  Contract: gpu_proto.h / web/src/gpu/proto.ts.
 *
 * Colours are premultiplied alpha.  Depth values are 0..1.  Fog is optional.
 * __wasm__: the import_module/import_name attributes are wasm-specific;
 * the native build provides stubs via nfsu2_register/host_abi.h.
 */
#ifndef NFSPS_GPU_PROTO_H
#define NFSPS_GPU_PROTO_H

#include <stdint.h>

/* ---- GwCtl: vsync / present control ---- */
typedef struct {
    uint32_t state;   /* 0 = stop, 1 = go, 2 = vsync pending */
    uint32_t frames;  /* total frames presented */
    uint32_t missed;  /* frames missed (guest slower than host) */
} GwCtl;

/* ---- GwVsync: frame timing ---- */
typedef struct {
    double frame_ms;      /* last frame's busy time, ms */
    double present_ms;    /* present wait, ms */
    double fps_ema;       /* exponential moving average */
    uint32_t frame_idx; /* monotonically increasing */
} GwVsync;

/* ---- GwDraw: a single draw command recorded by the guest ---- */
#define GW_MAX_MATRICES 4
#define GW_MAX_TEXTURES 4
#define GW_MAX_CONSTANTS 256

typedef struct {
    uint32_t vs;          /* vertex shader index / code handle */
    uint32_t ps;          /* pixel shader index / code handle */
    uint32_t vb_offset;   /* offset into TVert storage arena */
    uint32_t vb_count;    /* number of TVerts */
    float proj[4];        /* projection matrix */
    float view[4][4];     /* view (camera) matrix */
    float world[4][4];    /* world matrix (first matrix, if multiple) */
    float texture_slots[GW_MAX_TEXTURES]; /* texture binding indices */
    uint32_t constant_count; /* number of uploaded constants */
    float constants[GW_MAX_CONSTANTS][4]; /* uploaded constant data */
    uint32_t blend_state; /* blend mode */
    uint32_t depth_stencil_state; /* depth/stencil state */
    uint32_t rasterizer_state; /* cull/wind order */
    uint32_t stencil_ref; /* stencil reference value */
} GwDraw;

/* ---- GwState: per-frame renderer state ---- */
typedef struct {
    uint32_t target;      /* render target index / handle */
    uint32_t blend;       /* blend mode */
    uint32_t depth;       /* depth test mode (1 = on, 0 = off) */
    uint32_t stencil;     /* stencil test mode */
    uint32_t cull;        /* cull mode (0=none, 1=ccw, 2=cw) */
    uint32_t clear_argb;  /* clear colour ARGB */
    float fog[4];         /* fog colour/parameters */
    /* HUD state */
    uint32_t hud_speed;    /* current speed km/h */
    uint32_t hud_gear;     /* current gear */
    uint32_t hud_lap_time; /* lap time in ms */
    uint32_t hud_position; /* position on track */
} GwState;

/* ---- Host import / export signatures ---- */
/* The page-side (web/src/host/game-worker.ts, module "nfsu2") provides: */
#if defined(__wasm__)
__attribute__((import_module("nfsu2"), import_name("register")))
int nfsu2_register(int kind, void *ptr, uint32_t size);
#else
/* Native build: declare as external (implemented by host_abi.c) */
int nfsu2_register(int kind, void *ptr, uint32_t size);
#endif

/* Kind values match gpu_proto.h */
#define HOST_KIND_GPU    1

/* The page provides now_ms and blocked_ms (already in host_abi.h) */
/* The page provides the WebGPU device and encoder via these: */
/* Import a WebGPU device (set up by the page's start page) */
#if defined(__wasm__)
__attribute__((import_module("nfsu2"), import_name("gpu_device")))
int nfsu2_gpu_device(void);
#else
/* Native build: declare as external */
int nfsu2_gpu_device(void);
#endif

/* Present the rendered frame; the page supplies the canvas/surface */
#if defined(__wasm__)
__attribute__((import_module("nfsu2"), import_name("gpu_present")))
int nfsu2_gpu_present(uint32_t *back_buffer_argb, uint32_t width, uint32_t height);
#else
/* Native build: declare as external */
int nfsu2_gpu_present(uint32_t *back_buffer_argb, uint32_t width, uint32_t height);
#endif

/* Report frame timing to the page */
#if defined(__wasm__)
__attribute__((import_module("nfsu2"), import_name("gpu_vsync")))
int nfsu2_gpu_vsync(double frame_ms, double present_ms);
#else
/* Native build: declare as external */
int nfsu2_gpu_vsync(double frame_ms, double present_ms);
#endif

/* ---- function declarations (defined in gpu_web.c) ---- */
void gpu_init(void);          /* called once at boot */
void gpu_shutdown(void);      /* called at exit */
void gpu_present(void);       /* called at the end of each frame */
void gpu_clear(uint32_t argb);/* clear render target */
void gpu_draw(const GwDraw *d);/* record a draw command */
void gpu_set_state(const GwState *s);/* set rasteriser / depth / blend state */
void gpu_upload_constants(const float *c, unsigned count);/* upload PS constants */
void gpu_upload_texture(uint32_t slot, const uint8_t *pixels, unsigned w, unsigned h);/* upload a texture */

/* ---- D3D9 → WebGPU mapping helpers (inline, in gpu_web.c) ---- */
/* Convert D3D9 blend to GwBlend */
#define D3DBLEND_ZERO           2
#define D3DBLEND_ONE          1
#define D3DBLEND_SRCCOLOR     3
#define D3DBLEND_INVSRCCOLOR  4
#define D3DBLEND_SRCALPHA     5
#define D3DBLEND_INVSRCALPHA  6
#define D3DBLEND_SRCALPHASAT  7

/* D3D9 multisample quality -> WebGPU sample count */
#define D3DMULTISAMPLE_NONE     0
#define D3DMULTISAMPLE_NONMASKABLE 1

/* D3D9 depth func -> WebGPU compare function */
#define D3DCOMPARISON_NEVER     1
#define D3DCOMPARISON_LESS      2
#define D3DCOMPARISON_EQUAL     3
#define D3DCOMPARISON_NOT_EQUAL 4
#define D3DCOMPARISON_LESS_EQUAL 5
#define D3DCOMPARISON_GREATER 6
#define D3DCOMPARISON_NOT_GREATER 7
#define D3DCOMPARISON_ALWAYS 8

/* D3D9 stencil op -> WebGPI stencil op */
#define D3DSTENCILOP_KEEP       1
#define D3DSTENCILOP_ZERO       2
#define D3DSTENCILOP_REPLACE    3
#define D3DSTENCILOP_INCR_SAT 4
#define D3DSTENCILOP_DECR_SAT 5

/* ---- WGSL shader storage layout (matches nfsu2-web) ---- */
/* group 0: VB (storage, vertex), K (VS constants dynamic), P (PS parameters dynamic)
 * group 1: t0..t3 textures, s0..s3 samplers */

#endif
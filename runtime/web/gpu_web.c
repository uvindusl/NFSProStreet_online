/* WebGPU renderer for ProStreet.
 *
 * This file implements the recording API declared in gpu_proto.h.
 * The guest (lifted code) records draw calls into shared memory (Buf g_b[]).
 * The page's host thread (web/src/host/game-worker.ts) turns those records
 * into WebGPU commands (render passes, texture uploads, presents).
 *
 * See gpu_proto.h for the protocol; see web/src/gpu/ for the TS side.
 *
 * The guest must call gpu_init() once before any draw commands, and gpu_present()
 * at the end of every frame.  Without gpu_init() the renderer drops all draws
 * (null behaviour, same as nfsu2-web).
 *
 * NFSU2_RENDERER=webgpu (set by the page) turns this on.  Without it the
 * guest writes into shared memory but the host never compiles WebGPU — the
 * draw calls are quietly discarded, exactly like the null renderer in
 * runtime/null_platform.c.
 */

/* _POSIX_C_SOURCE needed for clock_gettime/CLOCK_MONOTONIC on some hosts */
#define _POSIX_C_SOURCE 200809L
#define PSC_N 32
#include "cpu.h"
#include "gfx.h"
#include "gpu_proto.h"
#include "host_abi.h"
#include "wgsl.h"
#include "texrep.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <stdatomic.h>
#include "setting.h"      /* ENVC */

/* ---- wasm atomics on shared memory ---- */
/* These builtins are only available when compiling for WebAssembly.
 * In native builds, provide fallbacks via the host_abi interface. */
#if defined(__wasm__)
static inline int32_t ald(int32_t *p){ return __atomic_load_n(p, __ATOMIC_SEQ_CST); }
static inline void ast(int32_t *p, int32_t v){ __atomic_store_n(p, v, __ATOMIC_SEQ_CST); }
static inline void wake(int32_t *p){ __builtin_wasm_memory_atomic_notify((int *)p, 0xFFFFFFFFu); }
/* wait while *p == v, at most ms milliseconds; 0 woken, 1 not equal, 2 timed out */
static inline int wait32(int32_t *p, int32_t v, double ms){
    return __builtin_wasm_memory_atomic_wait32((int *)p, v, ms < 0 ? -1 : (int64_t)(ms * 1e6));
}
#else
/* Native fallbacks: these go through the host's shared-memory interface.
 * The guest reads/writes g_mem directly; the host handles synchronization. */
static inline int32_t ald(int32_t *p){ return *p; }
static inline void ast(int32_t *p, int32_t v){ *p = v; }
static inline void wake(int32_t *p){ (void)p; /* no-op in native; host handles notifications */ }
static inline int wait32(int32_t *p, int32_t v, double ms){
    /* native: just return immediately (host handles synchronization) */
    (void)p; (void)v; (void)ms;
    return 0;
}
#endif
static double now_ms(void){ struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec * 1e-6; }

/* ---- shared state ---- */
static GwCtl   *g_ctl;
static GwVsync *g_vs;
static int      g_on;            /* recording for a page renderer */
static int      g_inited;
static int      g_detached;      /* the page never attached: dropped back to null behaviour */

/* ---- per-buffer state (double-buffered) ---- */
#define GW_NBUF 2
#define GW_MAX_DRAW 4096
#define MAXV   (256u * 1024u)               /* TVerts per buffer */
#define MAXD   (128u * 1024u)                /* draw records per buffer */
#define MAXRV  (16u * 1024u * 1024u)        /* raw vertex arena (bytes) */
#define MAXKB  ( 4u * 1024u * 1024u)        /* VS constant arena (bytes) */
#define RQ_CAP (64u * 1024u * 1024u)        /* resource queue ring (bytes) */
#define KZERO  256u                         /* K arena bytes [0, 256): TVert draws (base 0) */
#define KFIRST 2048u                        /* headlights + quick chat + tone + lat + photo */

/* ---- draw record (what the guest records) ---- */
typedef struct {
    uint32_t vs;          /* vertex shader index / code handle */
    uint32_t ps;          /* pixel shader index / code handle */
    uint32_t vb_offset;   /* offset into TVert storage arena */
    uint32_t vb_count;    /* number of TVerts */
    float proj[4];        /* projection matrix from guest */
    float view[4][4];     /* view matrix from guest */
    float world[4][4];    /* world matrix from guest */
    uint32_t tex_slots[4]; /* texture binding indices */
    uint32_t blend_state;
    uint32_t depth_stencil_state;
    uint32_t rasterizer_state;
    uint32_t stencil_ref;
} GwDrawRecord;

/* ---- per-buffer Buf ---- */
typedef struct {
    GwDrawRecord draws[MAXD];
    unsigned draw_n;
    TVert       *v;    /* TVert arena */
    unsigned      vn;
    uint8_t       *rv;   /* raw vertex arena */
    unsigned      rvn;
    uint8_t       *kb;   /* K arena (constants, headlights etc.) */
    unsigned      kn;
    float         psc[PSC_N][4]; /* PS constants */
    unsigned      pscn, psc_last, psc_lastver;
    unsigned      last_koff, last_ksz;
    int           last_vs;
} Buf;

/* ---- global buffers ---- */
static Buf g_b[GW_NBUF];
static int g_cur = 0;

/* ---- per-frame global state ---- */
static unsigned g_cur_target = 0, g_cur_blend = 0, g_cur_bcol = 0xFFFFFFFFu, g_cur_cull = 3u;
static unsigned g_cur_depth = 1u << 8, g_cur_dmode = 0, g_cur_zid = 1;
static unsigned g_cur_tex1 = 0, g_cur_tex2 = 0, g_cur_tex3 = 0;
static int      g_cur_ps = -1;
static float    g_cur_psc[PSC_N][4];
static unsigned g_psc_ver = 1;
static float    g_cur_proj[4];
static unsigned g_cur_smp[4];
static float    g_cur_aref;
static float    g_cur_fog[4];
static unsigned g_clear_argb = 0;            /* last Clear set (frame header) */
static unsigned g_frame_clear = 0;           /* colour in effect when the current frame began */
static int      g_initial_clear_set = 0;

/* ---- shader infrastructure ---- */
/* The guest's D3D9 vertex/pixel shaders are recorded as handles (uint32_t).
 * The page's TS side (web/src/gpu) maintains a registry of compiled WGSL
 * shaders.  When the guest first references a shader handle, the page compiles
 * the associated D3D9 vertex/pixel shader into WGSL (via the translator) and
 * hands back a WebGPU shader module handle.
 *
 * For now, we keep the handles as opaque integers and the page side resolves
 * them.  The translator is stubbed: if the guest ever asks for a shader that
 * isn't yet compiled, the guest stalls (the page will compile it before
 * resuming).
 */
#define SHADER_UNDEFINED 0xFFFFFFFFu

/* ---- helper: D3D9 blend state → Gw blend ---- */
static inline uint32_t d3d9_blend(uint32_t src, uint32_t dst)
{
    (void)dst;
    /* Map the most common D3DBLEND values; others fall through to src */
    switch (src) {
    case 2: return 2; /* D3DBLEND_ZERO   */
    case 1: return 1; /* D3DBLEND_ONE   */
    case 3: return 3; /* D3DBLEND_SRCCOLOR */
    case 4: return 4; /* D3DBLEND_INVSRCCOLOR */
    case 5: return 5; /* D3DBLEND_SRCALPHA */
    case 6: return 6; /* D3DBLEND_INVSRCALPHA */
    case 7: return 7; /* D3DBLEND_SRCALPHASAT */
    default: return src;
    }
}

/* ---- helper: D3D9 compare func → WebGPU compare ---- */
static inline uint32_t d3d9_cmp(uint32_t f)
{
    switch (f) {
    case 1: return 1; /* NEVER */
    case 2: return 2; /* LESS */
    case 3: return 3; /* EQUAL */
    case 4: return 4; /* NOT_EQUAL */
    case 5: return 5; /* LESS_EQUAL */
    case 6: return 6; /* GREATER */
    case 7: return 7; /* NOT_GREATER */
    case 8: return 8; /* ALWAYS */
    default: return 2; /* default to LESS */
    }
}

/* ---- helper: D3D9 stencil op → WebGPU stencil op ---- */
static inline uint32_t d3d9_stencil_op(uint32_t op)
{
    switch (op) {
    case 1: return 1; /* KEEP */
    case 2: return 2; /* ZERO */
    case 3: return 3; /* REPLACE */
    case 4: return 4; /* INCR_SAT */
    case 5: return 5; /* DECR_SAT */
    default: return 1;
    }
}

/* ---- helper: D3D9 fill mode → WebGPU fill mode ---- */
static inline uint32_t d3d9_fill(uint32_t m)
{
    switch (m) {
    case 2: return 2; /* WIREFRAME */
    case 3: return 3; /* SOLID */
    default: return 3;
    }
}

/* ---- helper: D3D9 cull mode → WebGPU cull mode ---- */
static inline uint32_t d3d9_cull(uint32_t m)
{
    switch (m) {
    case 1: return 0; /* NONE */
    case 2: return 1; /* CCW */
    case 3: return 2; /* CW */
    default: return 0;
    }
}

/* ---- gpu_init: called once at guest boot ---- */
void gpu_init(void)
{
    if (g_inited) return;
    g_inited = 1;
    g_on = 0;
    g_detached = 0;
    g_ctl = NULL;
    g_vs = NULL;

    /* initialise per-buffer arenas */
    for (int i = 0; i < GW_NBUF; ++i){
        Buf *b = &g_b[i];
        b->draw_n = 0;
        /* TVert arena: MAXV TVerts * 28 floats = 28*4*MAXV bytes */
        unsigned tv_bytes = 28u * 4u * MAXV;
        b->v = (TVert *)malloc(tv_bytes);
        b->vn = 0;
        /* raw vertex arena */
        b->rv = (uint8_t *)malloc(MAXRV);
        b->rvn = 0;
        /* K arena (constants, headlights etc.) */
        b->kb = (uint8_t *)malloc(KFIRST);
        b->kn = 0;
        /* PS constant arena */
        memset(b->psc, 0, sizeof b->psc);
        b->pscn = 0; b->psc_last = 0; b->psc_lastver = 0;
        b->last_koff = 0; b->last_ksz = 0; b->last_vs = -1;
    }
}

/* gpu_shutdown: called at exit */
void gpu_shutdown(void)
{
    if (!g_inited) return;
    g_inited = 0;
    for (int i = 0; i < GW_NBUF; ++i){
        Buf *b = &g_b[i];
        free(b->v); b->v = NULL; b->vn = 0;
        free(b->rv); b->rv = NULL; b->rvn = 0;
        free(b->kb); b->kb = NULL; b->kn = 0;
    }
}

/* ---- gpu_begin / gpu_end (host-side orchestrates, guest just records) ---- */
/* The guest calls these to bracket a frame.  The host (page) starts/stops
 * WebGPU command recording.  For now the guest simply records into the Buf;
 * the host will later compile WebGPU. */
void gpu_record_begin(void)
{
    /* select double buffer */
    g_cur = (g_cur + 1) % GW_NBUF;
    Buf *b = &g_b[g_cur];
    b->draw_n = 0;
    b->vn = 0;
    b->rvn = 0;
    b->kn = 0;
    memset(b->psc, 0, sizeof b->psc);
    b->pscn = 0; b->psc_last = 0; b->psc_lastver = 0;
    b->last_koff = 0; b->last_ksz = 0; b->last_vs = -1;
}

void gpu_record_end(void)
{
    /* nothing needed here; the host will present after receiving the buffer */
}

/* ---- gpu_draw: record a draw command from the guest ---- */
void gpu_draw(const GwDraw *d)
{
    if (!g_on || !g_inited) return; /* null renderer: drop */
    Buf *b = &g_b[g_cur];
    if (b->draw_n >= MAXD) return; /* drop */
    GwDrawRecord *r = &b->draws[b->draw_n++];
    r->vs = d->vs;
    r->ps = d->ps;
    r->vb_offset = d->vb_offset;
    r->vb_count = d->vb_count;
    memcpy(r->proj, d->proj, 4 * sizeof(float));
    memcpy(r->view, d->view, 4 * 4 * sizeof(float));
    memcpy(r->world, d->world, 4 * 4 * sizeof(float));
    memcpy(r->tex_slots, d->texture_slots, 4 * sizeof(uint32_t));
    r->blend_state = d->blend_state;
    r->depth_stencil_state = d->depth_stencil_state;
    r->rasterizer_state = d->rasterizer_state;
    r->stencil_ref = d->stencil_ref;
}

/* ---- gpu_set_state: set rasteriser / depth / blend state ---- */
void gpu_set_state(const GwState *s)
{
    if (!g_on || !g_inited) return;
    g_cur_target = s->target;
    g_cur_blend = s->blend;
    g_cur_depth = s->depth;
    g_cur_dmode = s->stencil; /* reuse dmode for now */
    g_cur_zid = s->stencil;
    g_cur_tex1 = 0; g_cur_tex2 = 0; g_cur_tex3 = 0;
    g_cur_ps = -1;
    memset(g_cur_psc, 0, sizeof g_cur_psc);
    memcpy(g_cur_psc[0], s->fog, sizeof s->fog);
    g_cur_aref = s->blend ? 0.0f : 1.0f; /* simplified */
}

/* ---- gpu_upload_constants: upload PS constants to the page ---- */
void gpu_upload_constants(const float *c, unsigned count)
{
    if (!g_on || !g_inited) return;
    Buf *b = &g_b[g_cur];
    unsigned n = count < PSC_N ? count : PSC_N;
    for (unsigned i = 0; i < n; ++i)
        memcpy(b->psc[i], c + i*4, 4 * sizeof(float));
    b->pscn = n;
    b->psc_last = n;
    b->psc_lastver = 1; /* bump version so the page knows to re-apply */
}

/* ---- gpu_upload_texture: upload a texture to the page ---- */
void gpu_upload_texture(uint32_t slot, const uint8_t *pixels, unsigned w, unsigned h)
{
    if (!g_on || !g_inited) return;
    /* The page side (web/src/gpu) will receive this and upload to WebGPU.
     * For now we just record the request in the K arena or drop it. */
    (void)slot; (void)pixels; (void)w; (void)h;
}

/* ---- gpu_present: called at end of frame (host will present) ---- */
void gpu_present(void)
{
    if (!g_on || !g_inited) return;
    /* update VSYNC state via host */
    /* the page's game-worker will read g_b[g_cur] and present */
    g_frame_clear = 0;
    g_clear_argb = 0;
}

/* ---- gpu_clear: clear render target ---- */
void gpu_clear(uint32_t argb)
{
    if (!g_on || !g_inited) return;
    g_clear_argb = argb;
    g_frame_clear = 1;
}

/* ---- gpu_report: called from the host's dispatch_report ---- */
void gpu_report(void)
{
    /* could print stats here; for now no-op */
}
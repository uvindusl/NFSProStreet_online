/* Offline software renderer, a debugging aid.
 *
 * PROSTREET_FRAMEDUMP=N renders frame N in software and writes the back
 * buffer to out/frame_N.ppm at Present (and every render target the frame
 * drew to as out/frame_N_rt_<surface>.ppm).  It interprets the D3D9 vertex
 * and pixel shader bytecode the effects set, so it checks the whole chain
 * (vertex declarations, shader constants, textures, render states) without a
 * GPU.  It is slow and approximate: level 0 of 2D textures only, no clipping
 * beyond dropping triangles behind the eye, derivatives are zero.
 *
 * The device state is tracked from the first call when the variable is set;
 * drawing happens only in frame N.  Render targets are the surfaces' own
 * storage in guest memory, so a texture that was rendered to samples like
 * any other. */
#include "rt.h"
#include "cpu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/stat.h>

extern uint32_t com_tex_level0(uint32_t tex, uint32_t *w, uint32_t *h, uint32_t *fmt, uint32_t *pitch);
extern uint32_t com_surf_data(uint32_t surf, uint32_t *w, uint32_t *h, uint32_t *fmt, uint32_t *pitch);
extern uint32_t com_buf_data(uint32_t buf, uint32_t *len, uint32_t *fmt);

static long g_want = -2;
static long g_frame;
static uint32_t rs[256], samp[20][16], tex[20];
static uint32_t cur_vs, cur_ps, cur_decl, cur_ib, cur_rt;
static struct { uint32_t vb, off, stride; } stream[16];
static float vsc[256][4], psc[224][4];
static struct { uint32_t x, y, w, h; float minz, maxz; } vp;
static float *zbuf; static uint32_t zw, zh;
static uint32_t rts[32]; static int nrts;
static unsigned n_draws, n_tris, n_pixels, n_skipped;

int sw_enabled(void) {
    if (g_want == -2) {
        const char *e = getenv("PROSTREET_FRAMEDUMP");
        g_want = e ? atol(e) : -1;
        rs[7] = 1; rs[14] = 1; rs[23] = 4; rs[22] = 3; rs[19] = 2; rs[20] = 1; rs[168] = 0xf; rs[25] = 8; rs[171] = 1;
        for (int i = 0; i < 20; i++) { samp[i][1] = samp[i][2] = 1; samp[i][5] = samp[i][6] = 1; }
    }
    return g_want >= 0;
}

/* ---- textures ------------------------------------------------------------ */

typedef struct { uint32_t obj, w, h; uint8_t *px; } TexCache;     /* RGBA8 */
static TexCache tcache[32];

static void tcache_clear(void) {
    for (int i = 0; i < 32; i++) { free(tcache[i].px); tcache[i].px = NULL; tcache[i].obj = 0; }
}

static void decode_texture(TexCache *t, uint32_t d, uint32_t fmt, uint32_t pitch) {
    uint32_t w = t->w, h = t->h;
    const uint8_t *src = g_mem + d;
    int dxt = fmt == 0x31545844u ? 1 : (fmt == 0x33545844u || fmt == 0x32545844u) ? 3 : (fmt == 0x35545844u || fmt == 0x34545844u) ? 5 : 0;
    for (uint32_t y = 0; y < h; y++) for (uint32_t x = 0; x < w; x++) {
        uint8_t r = 0, g = 0, b = 0, a = 255;
        if (dxt) {
            const uint8_t *blk = src + ((size_t)(y / 4) * ((w + 3) / 4) + x / 4) * (dxt == 1 ? 8 : 16);
            const uint8_t *cb = dxt == 1 ? blk : blk + 8;
            unsigned c0 = cb[0] | cb[1] << 8, c1 = cb[2] | cb[3] << 8, k = (cb[4 + (y & 3)] >> ((x & 3) * 2)) & 3;
            int p[4][3] = { { (int)(c0 >> 11) * 255 / 31, (int)((c0 >> 5) & 63) * 255 / 63, (int)(c0 & 31) * 255 / 31 },
                            { (int)(c1 >> 11) * 255 / 31, (int)((c1 >> 5) & 63) * 255 / 63, (int)(c1 & 31) * 255 / 31 } };
            int four = c0 > c1 || dxt != 1;
            for (int i = 0; i < 3; i++) {
                p[2][i] = four ? (2 * p[0][i] + p[1][i]) / 3 : (p[0][i] + p[1][i]) / 2;
                p[3][i] = four ? (p[0][i] + 2 * p[1][i]) / 3 : 0;
            }
            r = (uint8_t)p[k][0]; g = (uint8_t)p[k][1]; b = (uint8_t)p[k][2];
            if (dxt == 1 && !four && k == 3) a = 0;
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
            const uint8_t *px = src + (size_t)y * pitch;
            switch (fmt) {
            case 21: px += 4 * x; b = px[0]; g = px[1]; r = px[2]; a = px[3]; break;
            case 22: px += 4 * x; b = px[0]; g = px[1]; r = px[2]; break;
            case 20: px += 3 * x; b = px[0]; g = px[1]; r = px[2]; break;
            case 50: r = g = b = px[x]; break;
            case 28: r = g = b = 255; a = px[x]; break;
            case 51: px += 2 * x; r = g = b = px[0]; a = px[1]; break;
            case 23: { unsigned v = px[2 * x] | px[2 * x + 1] << 8; r = (uint8_t)((v >> 11) * 255 / 31); g = (uint8_t)(((v >> 5) & 63) * 255 / 63); b = (uint8_t)((v & 31) * 255 / 31); break; }
            case 26: { unsigned v = px[2 * x] | px[2 * x + 1] << 8; a = (uint8_t)((v >> 12) * 17); r = (uint8_t)(((v >> 8) & 15) * 17); g = (uint8_t)(((v >> 4) & 15) * 17); b = (uint8_t)((v & 15) * 17); break; }
            default: r = 255; g = 0; b = 255; break;          /* a format not decoded: magenta */
            }
        }
        uint8_t *o = t->px + 4 * ((size_t)y * w + x);
        o[0] = r; o[1] = g; o[2] = b; o[3] = a;
    }
}

static TexCache *get_texture(uint32_t obj) {
    if (!obj) return NULL;
    TexCache *slot = NULL;
    for (int i = 0; i < 32; i++) {
        if (tcache[i].obj == obj) return tcache[i].px ? &tcache[i] : NULL;
        if (!tcache[i].obj && !slot) slot = &tcache[i];
    }
    if (!slot) { tcache_clear(); slot = &tcache[0]; }
    uint32_t w, h, fmt, pitch, d = com_tex_level0(obj, &w, &h, &fmt, &pitch);
    slot->obj = obj; slot->w = w; slot->h = h; slot->px = NULL;
    if (!d || !w || !h || w > 8192 || h > 8192) return NULL;
    slot->px = malloc((size_t)w * h * 4);
    decode_texture(slot, d, fmt, pitch);
    return slot;
}

static int wrap(int i, int n, uint32_t mode) {
    if (mode == 1) { i %= n; return i < 0 ? i + n : i; }
    if (mode == 2) { int p = 2 * n; i %= p; if (i < 0) i += p; return i < n ? i : p - 1 - i; }
    return i < 0 ? 0 : i >= n ? n - 1 : i;
}

static void sample(int stage, float u, float v, float out[4]) {
    TexCache *t = get_texture(tex[stage]);
    if (!t) { out[0] = out[1] = out[2] = 0; out[3] = 1; return; }
    uint32_t au = samp[stage][1], av = samp[stage][2];
    float fx = u * (float)t->w - 0.5f, fy = v * (float)t->h - 0.5f;
    if (samp[stage][5] != 2) {                                /* point */
        int x = wrap((int)floorf(fx + 0.5f), (int)t->w, au), y = wrap((int)floorf(fy + 0.5f), (int)t->h, av);
        const uint8_t *p = t->px + 4 * ((size_t)y * t->w + x);
        for (int i = 0; i < 4; i++) out[i] = p[i] / 255.0f;
        return;
    }
    int x0 = (int)floorf(fx), y0 = (int)floorf(fy);
    float tx = fx - (float)x0, ty = fy - (float)y0;
    const uint8_t *p[4];
    for (int k = 0; k < 4; k++)
        p[k] = t->px + 4 * ((size_t)wrap(y0 + (k >> 1), (int)t->h, av) * t->w + wrap(x0 + (k & 1), (int)t->w, au));
    for (int i = 0; i < 4; i++)
        out[i] = ((p[0][i] * (1 - tx) + p[1][i] * tx) * (1 - ty) + (p[2][i] * (1 - tx) + p[3][i] * tx) * ty) / 255.0f;
}

/* ---- shader interpreter --------------------------------------------------- */

typedef struct {
    float r[32][4], in[16][4], t[8][4], c[256][4];
    float pos[4], fog[4], od[2][4], ot[8][4], o[16][4], oc[4][4], a0[4];
    int ci[16][4];
} M;

typedef struct { int usage, index; } Sem;
typedef struct {
    const uint32_t *code; int is_ps, major;
    Sem in[16];                 /* VS: v# -> vertex element; PS 3.0: v# -> varying */
    Sem out[16];                /* VS 3.0: o# -> varying */
    int nin, nout;
} Sh;

static int ilen(const Sh *s, const uint32_t *p) {
    uint32_t op = *p & 0xFFFFu;
    if (op == 0xFFFEu) return 1 + (int)(*p >> 16);
    if (s->major >= 2) return 1 + (int)((*p >> 24) & 0xF);
    if (op == 81) return 6;
    int n = 1;
    while (p[n] & 0x80000000u) n++;
    return n;
}

static int sh_open(Sh *s, uint32_t obj) {
    memset(s, 0, sizeof *s);
    for (int i = 0; i < 16; i++) s->in[i].usage = s->out[i].usage = -1;
    if (!obj || !mem_is_committed(obj + 8) || !MR32(obj + 4)) return 0;
    s->code = (const uint32_t *)(g_mem + MR32(obj + 4));
    s->is_ps = (s->code[0] >> 16) == 0xFFFFu;
    s->major = (int)((s->code[0] >> 8) & 0xFF);
    for (const uint32_t *p = s->code + 1; (*p & 0xFFFFu) != 0xFFFFu; p += ilen(s, p)) {
        if ((*p & 0xFFFFu) != 31) continue;                   /* dcl usage, register */
        uint32_t reg = p[2], type = ((reg >> 28) & 7) | ((reg >> 8) & 0x18), n = reg & 0x7FF;
        Sem sem = { (int)(p[1] & 0x1F), (int)((p[1] >> 16) & 0xF) };
        if (type == 1 && n < 16) s->in[n] = sem;
        if (type == 6 && n < 16 && !s->is_ps && s->major >= 3) s->out[n] = sem;
    }
    return 1;
}

static void load_consts(const Sh *s, M *m) {
    memset(m, 0, sizeof *m);
    memcpy(m->c, s->is_ps ? psc : vsc, s->is_ps ? sizeof psc : sizeof vsc);
    for (const uint32_t *p = s->code + 1; (*p & 0xFFFFu) != 0xFFFFu; p += ilen(s, p)) {
        uint32_t op = *p & 0xFFFFu, n = p[1] & 0x7FF;
        if (op == 81 && n < 256) memcpy(m->c[n], p + 2, 16);
        if (op == 48 && n < 16) memcpy(m->ci[n], p + 2, 16);
    }
}

static float *reg_ptr(const Sh *s, M *m, uint32_t tok, int write) {
    static float sink[4];
    uint32_t type = ((tok >> 28) & 7) | ((tok >> 8) & 0x18), n = tok & 0x7FF;
    switch (type) {
    case 0: return n < 32 ? m->r[n] : sink;
    case 1: return n < 16 ? m->in[n] : sink;
    case 2: return n < 256 ? m->c[n] : sink;
    case 3: return s->is_ps ? (n < 8 ? m->t[n] : sink) : m->a0;
    case 4: return n == 0 ? m->pos : n == 1 ? m->fog : sink;
    case 5: return n < 2 ? m->od[n] : sink;
    case 6: return s->major >= 3 ? (n < 16 ? m->o[n] : sink) : (n < 8 ? m->ot[n] : sink);
    case 8: return n < 4 ? m->oc[n] : sink;
    default: (void)write; return sink;
    }
}

/* read a source parameter (and its relative-addressing token), advance *pp */
static void rd_src(const Sh *s, M *m, const uint32_t **pp, float v[4]) {
    uint32_t tok = *(*pp)++;
    const float *r;
    static const float zero[4];
    if (tok & 0x2000u) {                                      /* c[a0.x + n] */
        int comp = 0;
        if (s->major >= 2) { uint32_t rel = *(*pp)++; comp = (int)((rel >> 16) & 3); }
        int idx = (int)(tok & 0x7FF) + (int)lrintf(m->a0[comp]);
        r = (idx >= 0 && idx < 256) ? m->c[idx] : zero;
    } else r = reg_ptr(s, m, tok, 0);
    uint32_t sw = (tok >> 16) & 0xFF, mod = (tok >> 24) & 0xF;
    for (int i = 0; i < 4; i++) {
        float x = r[(sw >> (2 * i)) & 3];
        switch (mod) {
        case 1: x = -x; break;
        case 2: x -= 0.5f; break;
        case 3: x = -(x - 0.5f); break;
        case 4: x = 2 * (x - 0.5f); break;
        case 5: x = -2 * (x - 0.5f); break;
        case 6: x = 1 - x; break;
        case 7: x *= 2; break;
        case 8: x *= -2; break;
        case 11: x = fabsf(x); break;
        case 12: x = -fabsf(x); break;
        default: break;
        }
        v[i] = x;
    }
}

/* Run a shader.  Returns 0 if the pixel was discarded. */
static int sh_run(const Sh *s, M *m) {
    const uint32_t *loop_pc[4]; int loop_n[4], depth = 0;
    for (const uint32_t *p = s->code + 1; (*p & 0xFFFFu) != 0xFFFFu; ) {
        uint32_t op = *p & 0xFFFFu;
        const uint32_t *next = p + ilen(s, p), *q = p + 1;
        if (op == 0xFFFEu || op == 0 || op == 31 || op == 81 || op == 48 || op == 47) { p = next; continue; }
        if (op == 38) {                                       /* rep i# */
            uint32_t n = *q & 0x7FF;
            if (depth < 4) { loop_pc[depth] = next; loop_n[depth] = n < 16 ? m->ci[n][0] : 0; depth++; }
            p = next; continue;
        }
        if (op == 39) {                                       /* endrep */
            if (depth > 0 && --loop_n[depth - 1] > 0) { p = loop_pc[depth - 1]; continue; }
            if (depth > 0) depth--;
            p = next; continue;
        }
        uint32_t dtok = *q++;
        float a[4] = {0}, b[4] = {0}, c[4] = {0}, o[4] = {0};
        if (op == 65) {                                       /* texkill */
            const float *r = reg_ptr(s, m, dtok, 0);
            if (r[0] < 0 || r[1] < 0 || r[2] < 0) return 0;
            p = next; continue;
        }
        if (q < next) rd_src(s, m, &q, a);
        if (op == 66) {                                       /* texld dst, coord, sampler */
            uint32_t st = q < next ? (*q & 0x7FF) : (dtok & 0x7FF);
            sample((int)(s->is_ps ? st : 16 + st) % 20, a[0], a[1], o);
        } else {
            if (q < next) rd_src(s, m, &q, b);
            if (q < next) rd_src(s, m, &q, c);
            float d;
            switch (op) {
            case 1: case 46: memcpy(o, a, 16); break;
            case 2: for (int i = 0; i < 4; i++) o[i] = a[i] + b[i]; break;
            case 3: for (int i = 0; i < 4; i++) o[i] = a[i] - b[i]; break;
            case 4: for (int i = 0; i < 4; i++) o[i] = a[i] * b[i] + c[i]; break;
            case 5: for (int i = 0; i < 4; i++) o[i] = a[i] * b[i]; break;
            case 6: for (int i = 0; i < 4; i++) o[i] = a[i] != 0 ? 1.0f / a[i] : 3.4e38f; break;
            case 7: for (int i = 0; i < 4; i++) o[i] = a[i] != 0 ? 1.0f / sqrtf(fabsf(a[i])) : 3.4e38f; break;
            case 8: d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; o[0] = o[1] = o[2] = o[3] = d; break;
            case 9: d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3]; o[0] = o[1] = o[2] = o[3] = d; break;
            case 10: for (int i = 0; i < 4; i++) o[i] = a[i] < b[i] ? a[i] : b[i]; break;
            case 11: for (int i = 0; i < 4; i++) o[i] = a[i] > b[i] ? a[i] : b[i]; break;
            case 12: for (int i = 0; i < 4; i++) o[i] = a[i] < b[i] ? 1.0f : 0.0f; break;
            case 13: for (int i = 0; i < 4; i++) o[i] = a[i] >= b[i] ? 1.0f : 0.0f; break;
            case 14: for (int i = 0; i < 4; i++) o[i] = powf(2.0f, a[i]); break;
            case 15: for (int i = 0; i < 4; i++) o[i] = a[i] != 0 ? log2f(fabsf(a[i])) : -3.4e38f; break;
            case 16: o[0] = 1; o[1] = a[0] > 0 ? a[0] : 0; o[2] = (a[0] > 0 && a[1] > 0) ? powf(a[1], a[3]) : 0; o[3] = 1; break;
            case 18: for (int i = 0; i < 4; i++) o[i] = a[i] * (b[i] - c[i]) + c[i]; break;
            case 19: for (int i = 0; i < 4; i++) o[i] = a[i] - floorf(a[i]); break;
            case 32: for (int i = 0; i < 4; i++) o[i] = powf(fabsf(a[i]), b[i]); break;
            case 33: o[0] = a[1] * b[2] - a[2] * b[1]; o[1] = a[2] * b[0] - a[0] * b[2]; o[2] = a[0] * b[1] - a[1] * b[0]; break;
            case 35: for (int i = 0; i < 4; i++) o[i] = fabsf(a[i]); break;
            case 36:
                d = a[0] * a[0] + a[1] * a[1] + a[2] * a[2];
                d = d > 0 ? 1.0f / sqrtf(d) : 0;
                for (int i = 0; i < 4; i++) o[i] = a[i] * d;
                break;
            case 37: o[0] = cosf(a[0]); o[1] = sinf(a[0]); break;
            case 88: for (int i = 0; i < 4; i++) o[i] = a[i] >= 0 ? b[i] : c[i]; break;
            case 90: d = a[0] * b[0] + a[1] * b[1] + c[0]; o[0] = o[1] = o[2] = o[3] = d; break;
            case 91: case 92: break;                          /* dsx, dsy: zero */
            default: { static uint32_t seen[8]; static int ns;
                       int known = 0; for (int i = 0; i < ns; i++) known |= seen[i] == op;
                       if (!known && ns < 8) { seen[ns++] = op; fprintf(stderr, "swrast: shader opcode %u not interpreted\n", op); } }
                     break;
            }
        }
        float *dst = reg_ptr(s, m, dtok, 1);
        if (op == 46) for (int i = 0; i < 4; i++) o[i] = floorf(o[i] + 0.5f);
        for (int i = 0; i < 4; i++)
            if (dtok & (0x10000u << i)) {
                float x = o[i];
                if (dtok & 0x100000u) x = x < 0 ? 0 : x > 1 ? 1 : x;
                dst[i] = x;
            }
        p = next;
    }
    return 1;
}

/* ---- vertices -------------------------------------------------------------- */

typedef struct { float pos[4]; float var[12][4]; float raw[2]; } Vtx;   /* raw: the POSITION attribute's x, y */         /* var: varyings in VS output order */
typedef struct { Sem sem[12]; int n; } Varyings;

static float half(uint16_t h) {
    int e = (h >> 10) & 31, mnt = h & 1023; float v;
    if (e == 0) v = ldexpf((float)mnt, -24);
    else if (e == 31) v = mnt ? NAN : INFINITY;
    else v = ldexpf((float)(mnt + 1024), e - 25);
    return (h & 0x8000) ? -v : v;
}

static void fetch_attr(const uint8_t *p, int type, float v[4]) {
    v[0] = v[1] = v[2] = 0; v[3] = 1;
    const int16_t *s = (const int16_t *)p; const uint16_t *us = (const uint16_t *)p;
    switch (type) {
    case 0: case 1: case 2: case 3: memcpy(v, p, 4u * (unsigned)(type + 1)); break;
    case 4: v[0] = p[2] / 255.0f; v[1] = p[1] / 255.0f; v[2] = p[0] / 255.0f; v[3] = p[3] / 255.0f; break;
    case 5: for (int i = 0; i < 4; i++) v[i] = p[i]; break;
    case 6: v[0] = s[0]; v[1] = s[1]; break;
    case 7: for (int i = 0; i < 4; i++) v[i] = s[i]; break;
    case 8: for (int i = 0; i < 4; i++) v[i] = p[i] / 255.0f; break;
    case 9: v[0] = s[0] / 32767.0f; v[1] = s[1] / 32767.0f; break;
    case 10: for (int i = 0; i < 4; i++) v[i] = s[i] / 32767.0f; break;
    case 11: v[0] = us[0] / 65535.0f; v[1] = us[1] / 65535.0f; break;
    case 12: for (int i = 0; i < 4; i++) v[i] = us[i] / 65535.0f; break;
    case 15: v[0] = half(us[0]); v[1] = half(us[1]); break;
    case 16: for (int i = 0; i < 4; i++) v[i] = half(us[i]); break;
    default: break;
    }
}

static const uint8_t *up_data; static uint32_t up_stride;      /* DrawPrimitiveUP stream 0 */

static void run_vertex(const Sh *vs, M *m, const Varyings *vy, uint32_t index, Vtx *out) {
    /* D3DVERTEXELEMENT9 { WORD stream, offset; BYTE type, method, usage, usageindex } */
    uint32_t el = cur_decl && mem_is_committed(cur_decl + 8) ? MR32(cur_decl + 4) : 0;
    for (int r = 0; r < 16; r++) {
        m->in[r][0] = m->in[r][1] = m->in[r][2] = 0; m->in[r][3] = 1;
        if (vs->in[r].usage < 0 || !el) continue;
        for (uint32_t e = el; mem_is_committed(e + 8) && MR16(e) != 0xFF; e += 8) {
            if (MR8(e + 6) != vs->in[r].usage || MR8(e + 7) != vs->in[r].index) continue;
            uint32_t st = MR16(e), off = MR16(e + 2);
            const uint8_t *base = NULL; uint32_t stride = 0;
            if (st == 0 && up_data) { base = up_data; stride = up_stride; }
            else if (st < 16 && stream[st].vb) {
                uint32_t len, fmt, d = com_buf_data(stream[st].vb, &len, &fmt);
                stride = stream[st].stride;
                if (d && (uint64_t)stream[st].off + (uint64_t)index * stride + off + 16 <= len + 16u)
                    base = g_mem + d + stream[st].off;
            }
            if (base) fetch_attr(base + (size_t)index * stride + off, MR8(e + 4), m->in[r]);
            break;
        }
    }
    for (int r = 0; r < 16; r++) if (vs->in[r].usage == 0 && vs->in[r].index == 0) { out->raw[0] = m->in[r][0]; out->raw[1] = m->in[r][1]; }
    memset(m->r, 0, sizeof m->r);
    sh_run(vs, m);
    memcpy(out->pos, m->pos, 16);
    for (int i = 0; i < vy->n; i++) {
        const float *src;
        if (vs->major >= 3) {
            src = m->o[0];
            for (int r = 0; r < 16; r++)
                if (vs->out[r].usage == vy->sem[i].usage && vs->out[r].index == vy->sem[i].index) { src = m->o[r]; break; }
        } else src = vy->sem[i].usage == 10 ? m->od[vy->sem[i].index & 1] : vy->sem[i].usage == 11 ? m->fog : m->ot[vy->sem[i].index & 7];
        memcpy(out->var[i], src, 16);
    }
    if (vs->major >= 3)
        for (int r = 0; r < 16; r++) if (vs->out[r].usage == 0 && vs->out[r].index == 0) memcpy(out->pos, m->o[r], 16);
}

/* ---- rasteriser ------------------------------------------------------------- */

static float blend_factor(uint32_t f, const float s[4], const float d[4], int i) {
    switch (f) {
    case 1: return 0; case 2: return 1;
    case 3: return s[i]; case 4: return 1 - s[i];
    case 5: return s[3]; case 6: return 1 - s[3];
    case 7: return d[3]; case 8: return 1 - d[3];
    case 9: return d[i]; case 10: return 1 - d[i];
    case 11: { float m2 = s[3] < 1 - d[3] ? s[3] : 1 - d[3]; return i == 3 ? 1 : m2; }
    default: return 1;
    }
}

static int cmp_func(uint32_t f, float a, float b) {
    switch (f) { case 1: return 0; case 2: return a < b; case 3: return a == b; case 4: return a <= b;
                 case 5: return a > b; case 6: return a != b; case 7: return a >= b; default: return 1; }
}

static void triangle(const Sh *ps, M *pm, const Varyings *vy, const Vtx *v0, const Vtx *v1, const Vtx *v2) {
    uint32_t w, h, fmt, pitch, d = com_surf_data(cur_rt, &w, &h, &fmt, &pitch);
    if (!d || (fmt != 21 && fmt != 22)) { n_skipped++; return; }
    const Vtx *v[3] = { v0, v1, v2 };
    float sx[3], sy[3], sz[3], iw[3];
    for (int i = 0; i < 3; i++) {
        if (v[i]->pos[3] <= 1e-6f) { n_skipped++; return; }    /* behind the eye: dropped, not clipped */
        iw[i] = 1.0f / v[i]->pos[3];
        sx[i] = (v[i]->pos[0] * iw[i] * 0.5f + 0.5f) * (float)vp.w + (float)vp.x;
        sy[i] = (0.5f - v[i]->pos[1] * iw[i] * 0.5f) * (float)vp.h + (float)vp.y;
        sz[i] = v[i]->pos[2] * iw[i];
    }
    float area = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sx[2] - sx[0]) * (sy[1] - sy[0]);
    if (area == 0 || (rs[22] == 3 && area < 0) || (rs[22] == 2 && area > 0)) return;
    float fx0 = fminf(sx[0], fminf(sx[1], sx[2])), fx1 = fmaxf(sx[0], fmaxf(sx[1], sx[2]));
    float fy0 = fminf(sy[0], fminf(sy[1], sy[2])), fy1 = fmaxf(sy[0], fmaxf(sy[1], sy[2]));
    int cx0 = (int)vp.x, cy0 = (int)vp.y, cx1 = (int)(vp.x + vp.w), cy1 = (int)(vp.y + vp.h);
    if (cx1 > (int)w) cx1 = (int)w;
    if (cy1 > (int)h) cy1 = (int)h;
    int x0 = (int)ceilf(fx0), x1 = (int)floorf(fx1), y0 = (int)ceilf(fy0), y1 = (int)floorf(fy1);
    if (x0 < cx0) x0 = cx0;
    if (y0 < cy0) y0 = cy0;
    if (x1 >= cx1) x1 = cx1 - 1;
    if (y1 >= cy1) y1 = cy1 - 1;
    int ztest = rs[7] && zbuf && zw == w && zh == h;
    n_tris++;
    for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) {
        float px = (float)x, py = (float)y, e[3];
        e[0] = ((sx[2] - sx[1]) * (py - sy[1]) - (sy[2] - sy[1]) * (px - sx[1])) / area;
        e[1] = ((sx[0] - sx[2]) * (py - sy[2]) - (sy[0] - sy[2]) * (px - sx[2])) / area;
        e[2] = 1 - e[0] - e[1];
        if (e[0] < 0 || e[1] < 0 || e[2] < 0) continue;
        /* top-left rule, roughly: a pixel exactly on a shared edge belongs to one side */
        if ((e[0] == 0 && (sy[2] - sy[1]) * area > 0) || (e[1] == 0 && (sy[0] - sy[2]) * area > 0) ||
            (e[2] == 0 && (sy[1] - sy[0]) * area > 0)) continue;
        float z = e[0] * sz[0] + e[1] * sz[1] + e[2] * sz[2];
        if (ztest && !cmp_func(rs[23], z, zbuf[(size_t)y * w + x])) continue;
        float pw = e[0] * iw[0] + e[1] * iw[1] + e[2] * iw[2];
        float bw[3] = { e[0] * iw[0] / pw, e[1] * iw[1] / pw, e[2] * iw[2] / pw };
        for (int i = 0; i < vy->n; i++) {
            float val[4];
            for (int k = 0; k < 4; k++) val[k] = bw[0] * v0->var[i][k] + bw[1] * v1->var[i][k] + bw[2] * v2->var[i][k];
            if (ps->major >= 3) {
                for (int r = 0; r < 16; r++)
                    if (ps->in[r].usage == vy->sem[i].usage && ps->in[r].index == vy->sem[i].index) memcpy(pm->in[r], val, 16);
            } else if (vy->sem[i].usage == 10) memcpy(pm->in[vy->sem[i].index & 1], val, 16);
            else if (vy->sem[i].usage == 5) memcpy(pm->t[vy->sem[i].index & 7], val, 16);
        }
        if (!sh_run(ps, pm)) continue;
        float *s = pm->oc[0];
        if (rs[15] && !cmp_func(rs[25], floorf(s[3] * 255.0f + 0.5f), (float)(rs[24] & 0xFF))) continue;
        uint8_t *o = g_mem + d + (size_t)y * pitch + 4u * (size_t)x;
        float dc[4] = { o[2] / 255.0f, o[1] / 255.0f, o[0] / 255.0f, o[3] / 255.0f }, out[4];
        for (int i = 0; i < 4; i++) {
            float sv = s[i] < 0 ? 0 : s[i] > 1 ? 1 : s[i];
            if (rs[27]) {
                int al = i == 3 && rs[206];
                float sf = blend_factor(al ? rs[207] : rs[19], s, dc, i), df = blend_factor(al ? rs[208] : rs[20], s, dc, i);
                uint32_t bop = rs[171] ? rs[171] : 1;
                sv = bop == 2 ? sv * sf - dc[i] * df : bop == 3 ? dc[i] * df - sv * sf
                   : bop == 4 ? fminf(sv, dc[i]) : bop == 5 ? fmaxf(sv, dc[i]) : sv * sf + dc[i] * df;
            }
            out[i] = sv < 0 ? 0 : sv > 1 ? 1 : sv;
        }
        uint32_t cw = rs[168];
        if (cw & 1) o[2] = (uint8_t)(out[0] * 255.0f + 0.5f);
        if (cw & 2) o[1] = (uint8_t)(out[1] * 255.0f + 0.5f);
        if (cw & 4) o[0] = (uint8_t)(out[2] * 255.0f + 0.5f);
        if (cw & 8) o[3] = (uint8_t)(out[3] * 255.0f + 0.5f);
        if (ztest && rs[14]) zbuf[(size_t)y * w + x] = z;
        n_pixels++;
    }
}

static void note_rt(uint32_t surf) {
    for (int i = 0; i < nrts; i++) if (rts[i] == surf) return;
    if (nrts < 32) rts[nrts++] = surf;
}

/* draw nprims primitives; idx(i) gives the i-th vertex index */
static void draw(uint32_t type, uint32_t nprims, const uint8_t *ib, int ib32, int32_t base, uint32_t first) {
    static Sh vs, ps; static M vm, pm;
    n_draws++;
    if (type < 4 || type > 6 || !nprims || nprims > 200000) return;
    if (!sh_open(&vs, cur_vs) || !sh_open(&ps, cur_ps) || !cur_rt) { n_skipped++; return; }
    note_rt(cur_rt);
    Varyings vy; vy.n = 0;
    if (vs.major >= 3) {
        for (int r = 0; r < 16 && vy.n < 12; r++) if (vs.out[r].usage > 0) vy.sem[vy.n++] = vs.out[r];
    } else {
        for (int i = 0; i < 2; i++) { vy.sem[vy.n].usage = 10; vy.sem[vy.n++].index = i; }
        for (int i = 0; i < 8; i++) { vy.sem[vy.n].usage = 5; vy.sem[vy.n++].index = i; }
    }
    load_consts(&vs, &vm);
    load_consts(&ps, &pm);
    uint32_t nv = type == 4 ? nprims * 3 : nprims + 2;
    Vtx *vt = malloc(sizeof(Vtx) * nv);
    for (uint32_t i = 0; i < nv; i++) {
        uint32_t k = first + i, index = ib ? (ib32 ? ((const uint32_t *)ib)[k] : ((const uint16_t *)ib)[k]) : k;
        run_vertex(&vs, &vm, &vy, (uint32_t)((int32_t)index + base), &vt[i]);
    }
    /* PROSTREET_FRAMEDUMP_FIT=K: draw number K is drawn with its vertices'
     * x/y fitted to the target instead of the vertex shader's position, on a
     * grey background.  For reading a draw whose transform is wrong. */
    { const char *fit = getenv("PROSTREET_FRAMEDUMP_FIT");
      if (fit && (unsigned)atoi(fit) == n_draws && nv) {
        float x0 = vt[0].raw[0], x1 = x0, y0 = vt[0].raw[1], y1 = y0;
        for (uint32_t i = 1; i < nv; i++) { x0 = fminf(x0, vt[i].raw[0]); x1 = fmaxf(x1, vt[i].raw[0]); y0 = fminf(y0, vt[i].raw[1]); y1 = fmaxf(y1, vt[i].raw[1]); }
        float sc = fmaxf((x1 - x0) / 1.8f, (y1 - y0) / 1.8f * ((float)vp.w / (float)vp.h));
        if (sc <= 0) sc = 1;
        for (uint32_t i = 0; i < nv; i++) {
            vt[i].pos[0] = (vt[i].raw[0] - (x0 + x1) / 2) / sc;
            vt[i].pos[1] = (vt[i].raw[1] - (y0 + y1) / 2) / sc * ((float)vp.w / (float)vp.h);
            vt[i].pos[2] = 0; vt[i].pos[3] = 1;
        }
        uint32_t w, h, fmt, pitch, d = com_surf_data(cur_rt, &w, &h, &fmt, &pitch);
        if (d && (fmt == 21 || fmt == 22)) memset(g_mem + d, 0x60, (size_t)pitch * h);
        rs[22] = 1;
        fprintf(stderr, "swrast: draw %u fitted: x %.3f..%.3f y %.3f..%.3f\n", n_draws, x0, x1, y0, y1);
      } }
    unsigned px0 = n_pixels, tr0 = n_tris;
    for (uint32_t i = 0; i < nprims; i++) {
        if (type == 4) triangle(&ps, &pm, &vy, &vt[3 * i], &vt[3 * i + 1], &vt[3 * i + 2]);
        else if (type == 5) { if (i & 1) triangle(&ps, &pm, &vy, &vt[i + 1], &vt[i], &vt[i + 2]); else triangle(&ps, &pm, &vy, &vt[i], &vt[i + 1], &vt[i + 2]); }
        else triangle(&ps, &pm, &vy, &vt[0], &vt[i + 1], &vt[i + 2]);
    }
    if (getenv("PROSTREET_FRAMEDUMP_LOG")) {
        fprintf(stderr, "swrast: draw %u: %s_%d_x + %s_%d_x, type %u, %u prims -> %u triangles, %u pixels; rt %08x vp %u,%u %ux%u; blend %u (%u,%u) ztest %u cull %u\n",
                n_draws, "vs", vs.major, "ps", ps.major, type, nprims, n_tris - tr0, n_pixels - px0, cur_rt,
                vp.x, vp.y, vp.w, vp.h, rs[27], rs[19], rs[20], rs[7], rs[22]);
        for (uint32_t i = 0; i < nv && i < 4; i++)
            fprintf(stderr, "    v%u pos %.3f %.3f %.3f %.3f  var0 %.3f %.3f %.3f %.3f  var2 %.3f %.3f %.3f %.3f\n", i,
                    vt[i].pos[0], vt[i].pos[1], vt[i].pos[2], vt[i].pos[3], vt[i].var[0][0], vt[i].var[0][1], vt[i].var[0][2], vt[i].var[0][3],
                    vt[i].var[2][0], vt[i].var[2][1], vt[i].var[2][2], vt[i].var[2][3]);
        if (getenv("PROSTREET_FRAMEDUMP_VERTS"))
            for (uint32_t i = 0; i < nv && i < 96; i++)
                fprintf(stderr, "    vert %u xy %.4f %.4f uv %.5f %.5f\n", i, vt[i].raw[0], vt[i].raw[1], vt[i].var[2][0], vt[i].var[2][1]);
        for (int st = 0; st < 4; st++) { uint32_t w, h, fmt, pitch; com_tex_level0(tex[st], &w, &h, &fmt, &pitch);
            if (tex[st]) fprintf(stderr, "    stage %d: texture %08x %ux%u format %x, filter %u, address %u/%u\n", st, tex[st], w, h, fmt, samp[st][5], samp[st][1], samp[st][2]); }
        for (int r = 0; r < 6; r++) fprintf(stderr, "    c%d %.4f %.4f %.4f %.4f\n", r, vsc[r][0], vsc[r][1], vsc[r][2], vsc[r][3]);
        for (int r = 0; r < 16; r++) if (vs.in[r].usage >= 0)
            fprintf(stderr, "    v%d usage %d.%d last value %.3f %.3f %.3f %.3f\n", r, vs.in[r].usage, vs.in[r].index, vm.in[r][0], vm.in[r][1], vm.in[r][2], vm.in[r][3]);
        { uint32_t el = cur_decl ? MR32(cur_decl + 4) : 0;
          for (uint32_t e = el; e && MR16(e) != 0xFF; e += 8) fprintf(stderr, "    decl stream %u off %u type %u usage %u.%u\n", MR16(e), MR16(e + 2), MR8(e + 4), MR8(e + 6), MR8(e + 7)); }
        fprintf(stderr, "    last pixel colour %.3f %.3f %.3f %.3f\n", pm.oc[0][0], pm.oc[0][1], pm.oc[0][2], pm.oc[0][3]);
    }
    free(vt);
}

static void write_ppm(const char *name, uint32_t surf) {
    uint32_t w, h, fmt, pitch, d = com_surf_data(surf, &w, &h, &fmt, &pitch);
    if (!d || (fmt != 21 && fmt != 22)) return;
    mkdir("out", 0755);
    FILE *f = fopen(name, "wb");
    if (!f) return;
    fprintf(f, "P6\n%u %u\n255\n", w, h);
    for (uint32_t y = 0; y < h; y++) for (uint32_t x = 0; x < w; x++) {
        const uint8_t *p = g_mem + d + (size_t)y * pitch + 4u * x;
        fputc(p[2], f); fputc(p[1], f); fputc(p[0], f);
    }
    fclose(f);
}

/* ---- the device calls -------------------------------------------------------- */

#define A(i) MR32(c->esp + 4u * (uint32_t)(i))

void sw_frame(long frame) { g_frame = frame; }

void sw_dev_call(CPU *c, const char *nm) {
    int live = g_frame == g_want;
    if (!strcmp(nm, "SetRenderState")) { if (A(1) < 256) rs[A(1)] = A(2); }
    else if (!strcmp(nm, "SetSamplerState")) {
        uint32_t st = A(1) >= 257 ? 16 + (A(1) - 257) : A(1);
        if (st < 20 && A(2) < 16) samp[st][A(2)] = A(3);
    } else if (!strcmp(nm, "SetTexture")) {
        uint32_t st = A(1) >= 257 ? 16 + (A(1) - 257) : A(1);
        if (st < 20) tex[st] = A(2);
    } else if (!strcmp(nm, "SetVertexShader")) cur_vs = A(1);
    else if (!strcmp(nm, "SetPixelShader")) cur_ps = A(1);
    else if (!strcmp(nm, "SetVertexShaderConstantF") || !strcmp(nm, "SetPixelShaderConstantF")) {
        int isps = nm[3] == 'P'; uint32_t max = isps ? 224 : 256;
        if (A(1) < max && A(3) <= max - A(1) && mem_is_committed(A(2)) && mem_is_committed(A(2) + 16 * A(3) - 1))
            memcpy(isps ? psc[A(1)] : vsc[A(1)], g_mem + A(2), 16u * A(3));
    } else if (!strcmp(nm, "SetVertexDeclaration")) cur_decl = A(1);
    else if (!strcmp(nm, "SetStreamSource")) { if (A(1) < 16) { stream[A(1)].vb = A(2); stream[A(1)].off = A(3); stream[A(1)].stride = A(4); } }
    else if (!strcmp(nm, "SetIndices")) cur_ib = A(1);
    else if (!strcmp(nm, "SetRenderTarget")) {
        if (A(1) == 0 && A(2)) {
            uint32_t w, h, fmt, pitch;
            cur_rt = A(2);
            if (live) tcache_clear();
            if (com_surf_data(cur_rt, &w, &h, &fmt, &pitch) || w) { vp.x = vp.y = 0; vp.w = w; vp.h = h; vp.minz = 0; vp.maxz = 1; }
        }
    } else if (!strcmp(nm, "SetViewport")) {
        if (mem_is_committed(A(1) + 20)) { vp.x = MR32(A(1)); vp.y = MR32(A(1) + 4); vp.w = MR32(A(1) + 8); vp.h = MR32(A(1) + 12); vp.minz = MRF32(A(1) + 16); vp.maxz = MRF32(A(1) + 20); }
    } else if (!live) return;
    else if (!strcmp(nm, "Clear")) {
        uint32_t w, h, fmt, pitch, d = com_surf_data(cur_rt, &w, &h, &fmt, &pitch), flags = A(3), col = A(4);
        if (d && (flags & 1) && (fmt == 21 || fmt == 22)) {
            note_rt(cur_rt);
            for (uint32_t y = vp.y; y < vp.y + vp.h && y < h; y++)
                for (uint32_t x = vp.x; x < vp.x + vp.w && x < w; x++) memcpy(g_mem + d + (size_t)y * pitch + 4u * x, &col, 4);
        }
        if ((flags & 2) && w && h) {
            if (zw != w || zh != h) { free(zbuf); zbuf = malloc(sizeof(float) * w * h); zw = w; zh = h; }
            float z = MRF32(c->esp + 20);
            for (size_t i = 0; i < (size_t)w * h; i++) zbuf[i] = z;
        }
    } else if (!strcmp(nm, "StretchRect")) {
        uint32_t sw_, sh_, sf, sp, sd = com_surf_data(A(1), &sw_, &sh_, &sf, &sp);
        uint32_t dw_, dh_, df, dp, dd = com_surf_data(A(3), &dw_, &dh_, &df, &dp);
        if (sd && dd && (sf == 21 || sf == 22) && (df == 21 || df == 22)) {
            uint32_t sr[4] = { 0, 0, sw_, sh_ }, dr[4] = { 0, 0, dw_, dh_ };
            if (A(2) && mem_is_committed(A(2) + 12)) memcpy(sr, g_mem + A(2), 16);
            if (A(4) && mem_is_committed(A(4) + 12)) memcpy(dr, g_mem + A(4), 16);
            note_rt(A(3)); tcache_clear();
            for (uint32_t y = dr[1]; y < dr[3] && y < dh_; y++) for (uint32_t x = dr[0]; x < dr[2] && x < dw_; x++) {
                uint32_t u = sr[0] + (uint32_t)((uint64_t)(x - dr[0]) * (sr[2] - sr[0]) / (dr[2] - dr[0]));
                uint32_t v = sr[1] + (uint32_t)((uint64_t)(y - dr[1]) * (sr[3] - sr[1]) / (dr[3] - dr[1]));
                if (u < sw_ && v < sh_) memcpy(g_mem + dd + (size_t)y * dp + 4u * x, g_mem + sd + (size_t)v * sp + 4u * u, 4);
            }
        }
    } else if (!strcmp(nm, "DrawPrimitive")) { up_data = NULL; draw(A(1), A(3), NULL, 0, 0, A(2)); }
    else if (!strcmp(nm, "DrawIndexedPrimitive")) {
        uint32_t len, fmt, d = com_buf_data(cur_ib, &len, &fmt);
        up_data = NULL;
        uint32_t nidx = A(1) == 4 ? A(6) * 3 : A(6) + 2, isz = fmt == 102 ? 4 : 2;
        if (d && (uint64_t)(A(5) + nidx) * isz <= len) draw(A(1), A(6), g_mem + d, fmt == 102, (int32_t)A(2), A(5));
        else n_skipped++;
    } else if (!strcmp(nm, "DrawPrimitiveUP")) {
        if (mem_is_committed(A(3))) { up_data = g_mem + A(3); up_stride = A(4); draw(A(1), A(2), NULL, 0, 0, 0); up_data = NULL; }
    } else if (!strcmp(nm, "DrawIndexedPrimitiveUP")) {
        if (mem_is_committed(A(5)) && mem_is_committed(A(7))) { up_data = g_mem + A(7); up_stride = A(8); draw(A(1), A(4), g_mem + A(5), A(6) == 102, 0, 0); up_data = NULL; }
    } else if (!strcmp(nm, "Present")) {
        char name[96];
        for (int i = 0; i < nrts; i++) {
            snprintf(name, sizeof name, "out/frame_%ld_rt%02d_%08x.ppm", g_want, i, rts[i]);
            write_ppm(name, rts[i]);
        }
        fprintf(stderr, "swrast: frame %ld: %u draws, %u triangles, %u pixels written, %u skipped; %d render targets in out/frame_%ld_rt*.ppm\n",
                g_want, n_draws, n_tris, n_pixels, n_skipped, nrts, g_want);
        tcache_clear();
    }
}

/* ID3DXEffect for compiled fx_2_0 effects (magic 0xFEFF0901).
 *
 * ProStreet loads 34 effects with D3DXCreateEffectFromResourceA and drives all
 * its drawing through them.  This file reads the effect binary (layout in
 * docs/fx-effect-format.md, decoded first by lifter/tools/fx_dump.py) and
 * implements the interface on top of it: parameters with their default
 * values, techniques, passes, annotations, and Begin/BeginPass/CommitChanges,
 * which send the pass's render states, shaders, shader constants, textures
 * and sampler states to the device.  The game installs its own
 * ID3DXEffectStateManager on every effect, so those go to the manager when
 * there is one and to the device otherwise.
 *
 * Not done yet: the expression VM.  States given by a compiled expression and
 * the preshaders (PRES) inside the shaders are skipped, so constants a
 * preshader computes are not set.  The counts are printed when an effect is
 * created.
 *
 * Handles are guest addresses of small records, so the game can store and
 * compare them; a handle that is not such a record is taken as a name, which
 * D3DX allows too. */
#include "rt.h"
#include "cpu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <pthread.h>

extern uint32_t gh_alloc(uint32_t n, int zero, uint32_t align);
extern uint32_t com_get_device(void);
extern uint32_t com_new_shader(uint32_t bytecode, uint32_t size);

#define FX_MAGIC      0xFEFF0901u
#define H_MAGIC       0x48584644u          /* 'DFXH' */
#ifndef D3DERR_INVALIDCALL
#define D3DERR_INVALIDCALL 0x8876086Cu
#endif
enum { HK_PARAM = 1, HK_TECH, HK_PASS, HK_ANN };
enum { ST_CONST = 0, ST_SHADER, ST_EXPR, ST_REF, ST_PARAM };   /* ST_PARAM: the value of parameter `ref` */

typedef struct {
    char name[64];
    uint16_t regset, reg, count, cls, type, rows, cols, elements;
    int param;                              /* index of the effect parameter of that name, or -1 */
} FxConst;

typedef struct {
    uint32_t bytecode, size, obj;           /* guest copy of the bytecode; device shader object */
    int is_ps, nconst, has_pres;
    FxConst *consts;
} FxShader;

typedef struct {
    uint32_t op, index, type, cls, val;
    int kind, ref;                          /* ST_*; ST_REF: parameter index */
    FxShader *sh;
} FxState;

typedef struct FxParam {
    char *name, *sem;
    uint32_t name_va, sem_va, handle;
    uint32_t type, cls, rows, cols, elements, flags;
    uint32_t n;                             /* value dwords: rows*cols*max(1,elements) */
    uint32_t *val;                          /* numeric value, row-major per element */
    uint32_t tex;                           /* texture parameters: the guest texture */
    uint32_t nstates;                       /* sampler parameters */
    FxState *states;
    uint32_t nann;
    struct FxParam *ann;
    uint32_t *elem_handles;
} FxParam;

typedef struct {
    char *name; uint32_t name_va, handle, nann, nstates;
    FxParam *ann; FxState *states;
    FxShader *vs, *ps;
} FxPass;

typedef struct {
    char *name; uint32_t name_va, handle, nann, npasses;
    FxParam *ann; FxPass *passes;
} FxTech;

typedef struct {
    char name[48];
    uint32_t pool;                          /* the ID3DXEffectPool it was created with */
    uint8_t *d; uint32_t len, cur;
    uint32_t nparams, ntechs;
    FxParam *params; FxTech *techs;
    int cur_tech, cur_pass;
    uint32_t manager, scratch;              /* guest state manager; guest scratch for constants */
    unsigned nexpr, npres;
    int ok;
} Fx;

enum { FX_MAX = 64 };
static Fx g_fx[FX_MAX];
static int g_nfx;
static pthread_mutex_t g_fx_lock = PTHREAD_MUTEX_INITIALIZER;

/* ---- reading the binary ------------------------------------------------ */

static uint32_t dw(Fx *fx, uint32_t o) {
    if (o + 4 > fx->len || o + 4 < o) { fx->ok = 0; return 0; }
    uint32_t v; memcpy(&v, fx->d + o, 4); return v;
}
static uint32_t rd(Fx *fx) { uint32_t v = dw(fx, fx->cur); fx->cur += 4; return v; }

static char *fx_string(Fx *fx, uint32_t off) {
    if (!off) return strdup("");
    uint32_t o = 8 + off, n = dw(fx, o);
    if (o + 4 + n > fx->len || n > 4096) { fx->ok = 0; return strdup(""); }
    char *s = malloc(n + 1);
    memcpy(s, fx->d + o + 4, n); s[n] = 0;
    return s;
}

static uint32_t gstr(const char *s) {
    if (!s || !*s) return 0;
    uint32_t n = (uint32_t)strlen(s) + 1, p = gh_alloc(n, 0, 1);
    if (p) memcpy(g_mem + p, s, n);
    return p;
}

static uint32_t new_handle(int kind, int fxi, uint32_t a, uint32_t b, uint32_t cidx) {
    uint32_t h = gh_alloc(24, 1, 4);
    if (!h) return 0;
    MR32(h) = H_MAGIC; MR32(h + 4) = (uint32_t)kind; MR32(h + 8) = (uint32_t)fxi;
    MR32(h + 12) = a; MR32(h + 16) = b; MR32(h + 20) = cidx;
    return h;
}

/* typedef { type, class, name_off, semantic_off, element_count, [cols, rows] } */
static void read_typedef(Fx *fx, uint32_t off, FxParam *p) {
    uint32_t o = 8 + off;
    p->type = dw(fx, o); p->cls = dw(fx, o + 4);
    p->name = fx_string(fx, dw(fx, o + 8)); p->sem = fx_string(fx, dw(fx, o + 12));
    p->elements = dw(fx, o + 16);
    p->rows = p->cols = 0;
    if (p->cls <= 3) { p->cols = dw(fx, o + 20); p->rows = dw(fx, o + 24); }
}

static void read_state(Fx *fx, uint32_t o, FxState *s) {
    FxParam td; memset(&td, 0, sizeof td);
    s->op = dw(fx, o); s->index = dw(fx, o + 4);
    read_typedef(fx, dw(fx, o + 8), &td);
    s->type = td.type; s->cls = td.cls;
    s->val = dw(fx, 8 + dw(fx, o + 12));
    s->kind = ST_CONST; s->ref = -1; s->sh = NULL;
    free(td.name); free(td.sem);
}

/* a parameter or annotation { typedef_off, value_off, ... }: typedef and default value */
static void read_value(Fx *fx, FxParam *p, uint32_t toff, uint32_t voff) {
    read_typedef(fx, toff, p);
    p->name_va = gstr(p->name); p->sem_va = gstr(p->sem);
    if (p->cls <= 3) {
        uint32_t per = p->rows * p->cols, ne = p->elements ? p->elements : 1;
        if (!per || per > 16 || ne > 4096) { fx->ok = 0; return; }
        p->n = per * ne;
        p->val = calloc(p->n, 4);
        for (uint32_t i = 0; i < p->n; i++) p->val[i] = dw(fx, 8 + voff + 4 * i);
    } else if (p->cls == 4 && p->type >= 10 && p->type <= 14) {     /* sampler { nstates, states } */
        uint32_t o = 8 + voff;
        p->nstates = dw(fx, o);
        if (p->nstates > 64) { fx->ok = 0; p->nstates = 0; return; }
        p->states = calloc(p->nstates ? p->nstates : 1, sizeof(FxState));
        for (uint32_t i = 0; i < p->nstates; i++) read_state(fx, o + 4 + 16 * i, &p->states[i]);
    }
}

static FxParam *read_annotations(Fx *fx, uint32_t n, int fxi) {
    if (n > 256) { fx->ok = 0; n = 0; }
    FxParam *a = calloc(n ? n : 1, sizeof(FxParam));
    for (uint32_t i = 0; i < n; i++) {
        uint32_t toff = rd(fx), voff = rd(fx);
        read_value(fx, &a[i], toff, voff);
        a[i].handle = new_handle(HK_ANN, fxi, 0, 0, 0);
    }
    return a;
}

/* The constant table (CTAB comment) of a shader: which registers its named
 * constants and samplers live in. */
static void read_ctab(Fx *fx, FxShader *sh) {
    const uint8_t *b = g_mem + sh->bytecode;
    uint32_t n = sh->size / 4;
    for (uint32_t i = 1; i < n; ) {
        uint32_t tok; memcpy(&tok, b + 4 * i, 4);
        if ((tok & 0xFFFFu) != 0xFFFEu) break;                 /* comments come first */
        uint32_t len = tok >> 16, id = 0;
        if (i + 1 + len > n) break;
        if (len) memcpy(&id, b + 4 * (i + 1), 4);
        if (id == 0x53455250u && len > 1) { sh->has_pres = 1; fx->npres++; }   /* 'PRES' with a body */
        if (id == 0x42415443u && len >= 8) {                            /* 'CTAB' */
            const uint8_t *t = b + 4 * (i + 2);
            uint32_t tsz = (len - 1) * 4, cnt, info;
            memcpy(&cnt, t + 12, 4); memcpy(&info, t + 16, 4);
            if (cnt > 256 || info + 20 * (uint64_t)cnt > tsz) break;
            sh->consts = calloc(cnt ? cnt : 1, sizeof(FxConst));
            for (uint32_t k = 0; k < cnt; k++) {
                const uint8_t *ci = t + info + 20 * k;
                FxConst *c = &sh->consts[sh->nconst];
                uint32_t nameo, typeo;
                memcpy(&nameo, ci, 4); memcpy(&c->regset, ci + 4, 2); memcpy(&c->reg, ci + 6, 2);
                memcpy(&c->count, ci + 8, 2); memcpy(&typeo, ci + 12, 4);
                if (nameo >= tsz || typeo + 12 > tsz) continue;
                snprintf(c->name, sizeof c->name, "%.*s", (int)(tsz - nameo), (const char *)t + nameo);
                memcpy(&c->cls, t + typeo, 2); memcpy(&c->type, t + typeo + 2, 2);
                memcpy(&c->rows, t + typeo + 4, 2); memcpy(&c->cols, t + typeo + 6, 2);
                memcpy(&c->elements, t + typeo + 8, 2);
                c->param = -1;
                for (uint32_t q = 0; q < fx->nparams; q++)
                    if (!strcmp(fx->params[q].name, c->name)) { c->param = (int)q; break; }
                sh->nconst++;
            }
        }
        i += 1 + len;
    }
}

/* A compiled expression (0x46580200 ...).  Every one in the game's effects is
 * a single move of a named parameter into the state (`MinFilter = <ciMinFilter>`):
 * a CTAB with one constant and an FXLC block with one `mov`.  Returns the
 * parameter's index, or -1 for anything more than that. */
static int expr_param(Fx *fx, uint32_t at, uint32_t n) {
    int param = -1, single_mov = 0;
    uint32_t nw = n / 4;
    for (uint32_t i = 1; i < nw; ) {
        uint32_t tok = dw(fx, at + 4 * i);
        if ((tok & 0xFFFFu) != 0xFFFEu) break;
        uint32_t len = tok >> 16, id = len ? dw(fx, at + 4 * (i + 1)) : 0;
        if (i + 1 + len > nw) return -1;
        uint32_t body = at + 4 * (i + 2);
        if (id == 0x42415443u && len >= 8) {                  /* 'CTAB' */
            uint32_t cnt = dw(fx, body + 12), info = dw(fx, body + 16);
            if (cnt != 1 || info + 20 > (len - 1) * 4) return -1;
            uint32_t nameo = dw(fx, body + info);
            if (nameo >= (len - 1) * 4) return -1;
            const char *nm = (const char *)fx->d + body + nameo;
            for (uint32_t q = 0; q < fx->nparams; q++)
                if (!strncmp(fx->params[q].name, nm, (len - 1) * 4 - nameo)) { param = (int)q; break; }
        } else if (id == 0x434C5846u && len >= 3) {           /* 'FXLC': count, then instructions */
            single_mov = dw(fx, body) == 1 && (dw(fx, body + 4) >> 20) == 0x100u;
        }
        i += 1 + len;
    }
    return single_mov ? param : -1;
}

static int fx_parse(Fx *fx, int fxi) {
    fx->ok = 1;
    if (fx->len < 16 || dw(fx, 0) != FX_MAGIC) return 0;
    fx->cur = 8 + dw(fx, 4);
    fx->nparams = rd(fx); fx->ntechs = rd(fx); rd(fx); rd(fx);
    if (!fx->ok || fx->nparams > 1024 || fx->ntechs > 256) return 0;
    fx->params = calloc(fx->nparams ? fx->nparams : 1, sizeof(FxParam));
    for (uint32_t i = 0; i < fx->nparams && fx->ok; i++) {
        FxParam *p = &fx->params[i];
        uint32_t toff = rd(fx), voff = rd(fx);
        p->flags = rd(fx); p->nann = rd(fx);
        read_value(fx, p, toff, voff);
        p->ann = read_annotations(fx, p->nann, fxi);
        p->handle = new_handle(HK_PARAM, fxi, i, 0xFFFFFFFFu, 0);
    }
    fx->techs = calloc(fx->ntechs ? fx->ntechs : 1, sizeof(FxTech));
    for (uint32_t i = 0; i < fx->ntechs && fx->ok; i++) {
        FxTech *t = &fx->techs[i];
        uint32_t nameo = rd(fx); t->nann = rd(fx); t->npasses = rd(fx);
        if (t->npasses > 64) return 0;
        t->name = fx_string(fx, nameo); t->name_va = gstr(t->name);
        t->ann = read_annotations(fx, t->nann, fxi);
        t->handle = new_handle(HK_TECH, fxi, i, 0, 0);
        t->passes = calloc(t->npasses ? t->npasses : 1, sizeof(FxPass));
        for (uint32_t k = 0; k < t->npasses && fx->ok; k++) {
            FxPass *ps = &t->passes[k];
            uint32_t pn = rd(fx); ps->nann = rd(fx); ps->nstates = rd(fx);
            if (ps->nstates > 256) return 0;
            ps->name = fx_string(fx, pn); ps->name_va = gstr(ps->name);
            ps->ann = read_annotations(fx, ps->nann, fxi);
            ps->handle = new_handle(HK_PASS, fxi, i, k, 0);
            ps->states = calloc(ps->nstates ? ps->nstates : 1, sizeof(FxState));
            for (uint32_t s = 0; s < ps->nstates; s++) read_state(fx, fx->cur + 16 * s, &ps->states[s]);
            fx->cur += 16 * ps->nstates;
        }
    }
    if (!fx->ok) return 0;
    /* object table: { nsmall, nresources }, parameter objects, then one record
     * per state whose value is a shader, an expression or a parameter name */
    uint32_t nsmall = rd(fx), nres = rd(fx);
    for (uint32_t i = 0; i < nsmall && fx->ok; i++) { rd(fx); uint32_t n = rd(fx); fx->cur += (n + 3) & ~3u; }
    for (uint32_t i = 0; i < nres && fx->ok; i++) {
        uint32_t ti = rd(fx), idx = rd(fx), el = rd(fx), si = rd(fx), usage = rd(fx), n = rd(fx);
        uint32_t at = fx->cur;
        (void)el;
        fx->cur += (n + 3) & ~3u;
        if (at + n > fx->len) return 0;
        FxState *st = NULL; FxPass *ps = NULL;
        if (ti == 0xFFFFFFFFu) {
            if (idx < fx->nparams && si < fx->params[idx].nstates) st = &fx->params[idx].states[si];
        } else if (ti < fx->ntechs && idx < fx->techs[ti].npasses && si < fx->techs[ti].passes[idx].nstates) {
            ps = &fx->techs[ti].passes[idx]; st = &ps->states[si];
        }
        if (!st) continue;
        uint32_t first = n >= 4 ? dw(fx, at) : 0;
        if (usage == 1) {
            st->kind = ST_REF;
            for (uint32_t q = 0; q < fx->nparams; q++)
                if (!strncmp(fx->params[q].name, (const char *)fx->d + at, n)) { st->ref = (int)q; break; }
        } else if (usage == 0 && ((first >> 16) == 0xFFFEu || (first >> 16) == 0xFFFFu) && ps) {
            FxShader *sh = calloc(1, sizeof *sh);
            sh->is_ps = (first >> 16) == 0xFFFFu;
            sh->size = n;
            sh->bytecode = gh_alloc(n, 0, 4);
            if (!sh->bytecode) return 0;
            memcpy(g_mem + sh->bytecode, fx->d + at, n);
            sh->obj = com_new_shader(sh->bytecode, n);
            read_ctab(fx, sh);
            st->kind = ST_SHADER; st->sh = sh;
            if (sh->is_ps) ps->ps = sh; else ps->vs = sh;
        } else {
            st->ref = (usage == 0 && first == 0x46580200u) ? expr_param(fx, at, n) : -1;
            if (st->ref >= 0 && fx->params[st->ref].val) st->kind = ST_PARAM;
            else { st->kind = ST_EXPR; fx->nexpr++; }
        }
    }
    return fx->ok;
}

/* ---- the resource ------------------------------------------------------- */

/* RT_RCDATA resource `name` (or integer id) of the image mapped at 0x00400000. */
static const uint8_t *find_rcdata(uint32_t name_va, uint32_t *size, char *label, size_t nlabel) {
    const uint32_t base = 0x00400000u;
    if (!mem_is_committed(base + 0x3c)) return NULL;
    uint32_t nt = base + MR32(base + 0x3c);
    if (!mem_is_committed(nt + 0x90)) return NULL;
    uint32_t rva = MR32(nt + 0x78 + 8 * 2);                 /* DataDirectory[RESOURCE] */
    if (!rva || !mem_is_committed(base + rva + 16)) return NULL;
    uint32_t root = base + rva, dir = root;
    char want[64] = "";
    uint32_t want_id = 0;
    if (name_va >= 0x10000u && mem_is_committed(name_va)) snprintf(want, sizeof want, "%s", (const char *)g_mem + name_va);
    else want_id = name_va;
    {   /* a label for logs: cut to fit */
        const char *src = want[0] ? want : "(id)";
        size_t k = strnlen(src, nlabel - 1);
        memcpy(label, src, k); label[k] = 0;
    }
    for (int level = 0; level < 3; level++) {
        uint32_t n = MR16(dir + 12) + MR16(dir + 14), next = 0;
        for (uint32_t i = 0; i < n && !next; i++) {
            uint32_t e = dir + 16 + 8 * i;
            if (!mem_is_committed(e + 8)) return NULL;
            uint32_t id = MR32(e), off = MR32(e + 4);
            int hit;
            if (level == 0) hit = id == 10u;
            else if (level == 2) hit = 1;
            else if (want[0]) {
                hit = 0;
                if (id & 0x80000000u) {
                    uint32_t s = root + (id & 0x7FFFFFFFu), len = MR16(s);
                    hit = len == strlen(want);
                    for (uint32_t k = 0; hit && k < len; k++) {
                        int a = MR16(s + 2 + 2 * k), b2 = (unsigned char)want[k];
                        if (a >= 'a' && a <= 'z') a -= 32;
                        if (b2 >= 'a' && b2 <= 'z') b2 -= 32;
                        hit = a == b2;
                    }
                }
            } else hit = id == want_id;
            if (hit) next = off;
        }
        if (!next) return NULL;
        if (level < 2) { if (!(next & 0x80000000u)) return NULL; dir = root + (next & 0x7FFFFFFFu); }
        else {
            uint32_t de = root + next;
            if (!mem_is_committed(de + 8)) return NULL;
            uint32_t drva = MR32(de), dsz = MR32(de + 4);
            if (!dsz || !mem_is_committed(base + drva) || !mem_is_committed(base + drva + dsz - 1)) return NULL;
            *size = dsz;
            return g_mem + base + drva;
        }
    }
    return NULL;
}

/* Called by D3DXCreateEffectFromResourceA.  Fills the COM object `obj` (its
 * first field after the vtable is the effect's index + 1).  Returns 1 if the
 * effect was read. */
int fx_create(uint32_t obj, uint32_t name_va, uint32_t pool) {
    char label[sizeof g_fx[0].name]; uint32_t size = 0;
    const uint8_t *res = find_rcdata(name_va, &size, label, sizeof label);
    pthread_mutex_lock(&g_fx_lock);
    int fxi = g_nfx < FX_MAX ? g_nfx++ : -1;
    pthread_mutex_unlock(&g_fx_lock);
    if (!res || fxi < 0) {
        fprintf(stderr, "d3dx: effect %s: %s\n", label, res ? "too many effects" : "resource not found");
        return 0;
    }
    Fx *fx = &g_fx[fxi];
    snprintf(fx->name, sizeof fx->name, "%s", label);
    fx->d = malloc(size); memcpy(fx->d, res, size); fx->len = size;
    fx->cur_tech = 0; fx->cur_pass = -1;
    if (!fx_parse(fx, fxi)) {
        fprintf(stderr, "d3dx: effect %s: not a readable fx_2_0 effect (%u bytes)\n", label, size);
        fx->nparams = fx->ntechs = 0;
        return 0;
    }
    fx->scratch = gh_alloc(256 * 16, 1, 16);
    MR32(obj + 4) = (uint32_t)fxi + 1u;
    /* Effects created with the same ID3DXEffectPool share the value of every
     * parameter declared `shared` (D3DX_PARAMETER_SHARED) with the same name.
     * The game relies on it: it sets WorldViewProj on whichever effect it has
     * at hand (the movie quad's, just before the 2D batch is flushed) and
     * draws with another.  Numeric values only; a shared texture is not. */
    fx->pool = pool;
    unsigned nshared = 0;
    pthread_mutex_lock(&g_fx_lock);
    for (uint32_t i = 0; pool && i < fx->nparams; i++) {
        FxParam *q = &fx->params[i];
        if (!(q->flags & 1u) || !q->val || !q->name) continue;
        for (int o = 0; o < fxi; o++) {
            Fx *of = &g_fx[o];
            if (of->pool != pool) continue;
            FxParam *m = 0;
            for (uint32_t j = 0; j < of->nparams; j++)
                if ((of->params[j].flags & 1u) && of->params[j].val && of->params[j].name &&
                    of->params[j].n == q->n && !strcmp(of->params[j].name, q->name)) { m = &of->params[j]; break; }
            if (m) { free(q->val); q->val = m->val; nshared++; break; }
        }
    }
    pthread_mutex_unlock(&g_fx_lock);
    if (nshared) fprintf(stderr, "d3dx: effect %s: %u parameters shared through pool %08x\n", label, nshared, pool);
    fprintf(stderr, "d3dx: effect %s: %u parameters, %u techniques; not evaluated: %u expression states, %u preshaders\n",
            label, fx->nparams, fx->ntechs, fx->nexpr, fx->npres);
    return 1;
}

/* ---- handles ------------------------------------------------------------ */

typedef struct { int kind; FxParam *p; int elem; FxTech *t; FxPass *ps; } Ref;

static FxParam *param_by_name(Fx *fx, const char *s, int *elem) {
    size_t n = strcspn(s, "[");
    for (uint32_t i = 0; i < fx->nparams; i++)
        if (strlen(fx->params[i].name) == n && !strncmp(fx->params[i].name, s, n)) {
            if (elem) *elem = s[n] == '[' ? atoi(s + n + 1) : -1;
            return &fx->params[i];
        }
    return NULL;
}

static int resolve(Fx *fx, uint32_t h, Ref *r) {
    memset(r, 0, sizeof *r); r->elem = -1;
    if (!h || !mem_is_committed(h)) return 0;
    if (mem_is_committed(h + 20) && MR32(h) == H_MAGIC) {
        if (MR32(h + 8) != (uint32_t)(fx - g_fx)) return 0;
        uint32_t a = MR32(h + 12), b = MR32(h + 16);
        r->kind = (int)MR32(h + 4);
        if (r->kind == HK_PARAM && a < fx->nparams) { r->p = &fx->params[a]; r->elem = b == 0xFFFFFFFFu ? -1 : (int)b; return 1; }
        if (r->kind == HK_TECH && a < fx->ntechs) { r->t = &fx->techs[a]; return 1; }
        if (r->kind == HK_PASS && a < fx->ntechs && b < fx->techs[a].npasses) { r->t = &fx->techs[a]; r->ps = &r->t->passes[b]; return 1; }
        if (r->kind == HK_ANN) {
            /* annotations are few: find the one that owns this handle */
            #define SCAN(arr, cnt) for (uint32_t q = 0; q < (cnt); q++) if ((arr)[q].handle == h) { r->p = &(arr)[q]; return 1; }
            for (uint32_t i = 0; i < fx->nparams; i++) SCAN(fx->params[i].ann, fx->params[i].nann)
            for (uint32_t i = 0; i < fx->ntechs; i++) {
                SCAN(fx->techs[i].ann, fx->techs[i].nann)
                for (uint32_t k = 0; k < fx->techs[i].npasses; k++) SCAN(fx->techs[i].passes[k].ann, fx->techs[i].passes[k].nann)
            }
            #undef SCAN
        }
        return 0;
    }
    const char *s = (const char *)g_mem + h;                 /* a name */
    if ((r->p = param_by_name(fx, s, &r->elem))) { r->kind = HK_PARAM; return 1; }
    for (uint32_t i = 0; i < fx->ntechs; i++)
        if (!strcmp(fx->techs[i].name, s)) { r->kind = HK_TECH; r->t = &fx->techs[i]; return 1; }
    return 0;
}

static FxParam *num_param(Fx *fx, uint32_t h, uint32_t **dst, uint32_t *n) {
    Ref r;
    if (!resolve(fx, h, &r) || !r.p || !r.p->val) return NULL;
    uint32_t per = r.p->rows * r.p->cols;
    if (r.elem >= 0) {
        if ((uint32_t)r.elem >= (r.p->elements ? r.p->elements : 1)) return NULL;
        *dst = r.p->val + (uint32_t)r.elem * per; *n = per;
    } else { *dst = r.p->val; *n = r.p->n; }
    return r.p;
}

/* ---- calling the device or the game's state manager ---------------------- */

static uint32_t gcall(CPU *c, uint32_t obj, unsigned idx, int n, const uint32_t *a) {
    for (int i = n - 1; i >= 0; i--) PUSH32(a[i]);
    PUSH32(obj);
    guest_call(c, MR32(MR32(obj) + 4u * idx));
    return c->eax;
}

enum { SM_RS, SM_TEX, SM_SAMP, SM_VS, SM_VSF, SM_PS, SM_PSF, SM_N };
static void sm(CPU *c, Fx *fx, int what, int n, const uint32_t *a) {
    static const unsigned mgr_idx[SM_N] = { 7, 8, 10, 13, 14, 17, 18 };
    static const unsigned dev_idx[SM_N] = { 57, 65, 69, 92, 94, 107, 109 };
    if (fx->manager && mem_is_committed(fx->manager) && mem_is_committed(MR32(fx->manager)))
        gcall(c, fx->manager, mgr_idx[what], n, a);
    else
        gcall(c, com_get_device(), dev_idx[what], n, a);
}

/* effect state operation -> D3DRENDERSTATETYPE (the ones the game's effects use) */
static int rs_of(uint32_t op) {
    switch (op) {
    case 0: return 7; case 3: return 14; case 6: return 19; case 7: return 20; case 8: return 22;
    case 9: return 23; case 13: return 27; case 14: return 28; case 73: return 168;
    case 99: return 206; case 100: return 207; case 101: return 208;
    default: return -1;
    }
}

static float as_float(const FxParam *p, uint32_t v) {
    float f;
    if (p->type == 3) { memcpy(&f, &v, 4); return f; }
    return p->type == 1 ? (v ? 1.0f : 0.0f) : (float)(int32_t)v;
}

/* The value of a state that is a constant or a parameter's value; 0 if neither. */
static int state_value(Fx *fx, const FxState *st, uint32_t *v) {
    if (st->kind == ST_CONST) { *v = st->val; return 1; }
    if (st->kind == ST_PARAM) {
        const FxParam *p = &fx->params[st->ref];
        float f; memcpy(&f, &p->val[0], 4);
        *v = p->type == 3 ? (uint32_t)(int32_t)f : p->val[0];
        return 1;
    }
    return 0;
}

/* Send the shader's named constants (from the effect's parameters), and its
 * samplers' textures and states. */
static void upload(CPU *c, Fx *fx, FxShader *sh) {
    if (!sh) return;
    for (int i = 0; i < sh->nconst; i++) {
        FxConst *k = &sh->consts[i];
        if (k->param < 0) continue;
        FxParam *p = &fx->params[k->param];
        if (k->regset == 2 && p->val && k->count && k->count <= 256) {
            float *out = (float *)(g_mem + fx->scratch);
            memset(out, 0, (size_t)k->count * 16);
            uint32_t per = p->rows * p->cols, ne = p->elements ? p->elements : 1, reg = 0;
            for (uint32_t e = 0; e < ne && reg < k->count; e++) {
                const uint32_t *v = p->val + e * per;
                if (p->cls == 2 && k->cls == 3) {             /* matrix, one register per column */
                    for (uint32_t col = 0; col < p->cols && col < (k->cols ? k->cols : 4u) && reg < k->count; col++, reg++)
                        for (uint32_t row = 0; row < p->rows && row < 4; row++)
                            out[reg * 4 + row] = as_float(p, v[row * p->cols + col]);
                } else if (p->cls == 2) {                     /* matrix, one register per row */
                    for (uint32_t row = 0; row < p->rows && row < (k->rows ? k->rows : 4u) && reg < k->count; row++, reg++)
                        for (uint32_t col = 0; col < p->cols && col < 4; col++)
                            out[reg * 4 + col] = as_float(p, v[row * p->cols + col]);
                } else {
                    for (uint32_t q = 0; q < per && q < 4; q++) out[reg * 4 + q] = as_float(p, v[q]);
                    reg++;
                }
            }
            uint32_t a[3] = { k->reg, fx->scratch, k->count };
            sm(c, fx, sh->is_ps ? SM_PSF : SM_VSF, 3, a);
        } else if (k->regset == 3 && p->states) {
            uint32_t stage = sh->is_ps ? k->reg : 257u + k->reg;      /* D3DVERTEXTEXTURESAMPLER0 */
            for (uint32_t s = 0; s < p->nstates; s++) {
                FxState *st = &p->states[s];
                if (st->op == 164) {
                    if (st->kind == ST_REF && st->ref >= 0) {
                        uint32_t a[2] = { stage, fx->params[st->ref].tex };
                        sm(c, fx, SM_TEX, 2, a);
                    }
                } else if (st->op >= 165 && st->op <= 177) {
                    uint32_t a[3] = { stage, st->op - 164u, 0 };
                    if (state_value(fx, st, &a[2])) sm(c, fx, SM_SAMP, 3, a);
                }
            }
        }
    }
}

static void apply_pass(CPU *c, Fx *fx, FxPass *ps, int states) {
    if (states) {
        for (uint32_t i = 0; i < ps->nstates; i++) {
            FxState *st = &ps->states[i];
            if (st->kind == ST_SHADER) {
                uint32_t a[1] = { st->sh->obj };
                sm(c, fx, st->sh->is_ps ? SM_PS : SM_VS, 1, a);
            } else if (st->kind == ST_CONST && (st->op == 146 || st->op == 147)) {
                uint32_t a[1] = { 0 };                        /* `PixelShader = NULL` */
                sm(c, fx, st->op == 147 ? SM_PS : SM_VS, 1, a);
            } else if (rs_of(st->op) >= 0) {
                uint32_t a[2] = { (uint32_t)rs_of(st->op), 0 };
                if (state_value(fx, st, &a[1])) sm(c, fx, SM_RS, 2, a);
            }
        }
    }
    upload(c, fx, ps->vs);
    upload(c, fx, ps->ps);
}

/* ---- the interface ------------------------------------------------------- */

#define A(i) MR32(c->esp + 4u * (uint32_t)(i))
#define OUT32(p, v) do { uint32_t p_ = (p); if (p_ && mem_is_committed(p_)) MR32(p_) = (v); } while (0)

static void set_comp(FxParam *p, uint32_t *dst, int src_kind, uint32_t v) {   /* src_kind: 1 bool, 2 int, 3 float */
    float f; memcpy(&f, &v, 4);
    if (p->type == 3) { float o = src_kind == 3 ? f : src_kind == 1 ? (v ? 1.0f : 0.0f) : (float)(int32_t)v; memcpy(dst, &o, 4); }
    else if (p->type == 1) *dst = src_kind == 3 ? (f != 0.0f) : (v != 0);
    else *dst = src_kind == 3 ? (uint32_t)(int32_t)f : v;
}
static uint32_t get_comp(const FxParam *p, uint32_t v, int dst_kind) {
    float f; memcpy(&f, &v, 4);
    if (dst_kind == 3) { float o = as_float(p, v); uint32_t u; memcpy(&u, &o, 4); return u; }
    if (dst_kind == 1) return p->type == 3 ? (f != 0.0f) : (v != 0);
    return p->type == 3 ? (uint32_t)(int32_t)f : v;
}

static void desc_param(const FxParam *p, uint32_t pd, int elem) {
    if (!pd || !mem_is_committed(pd + 40)) return;
    uint32_t per = p->rows * p->cols;
    MR32(pd + 0) = p->name_va; MR32(pd + 4) = p->sem_va;
    MR32(pd + 8) = p->cls; MR32(pd + 12) = p->type;
    MR32(pd + 16) = p->rows; MR32(pd + 20) = p->cols;
    MR32(pd + 24) = elem >= 0 ? 0 : p->elements; MR32(pd + 28) = p->nann;
    MR32(pd + 32) = 0; MR32(pd + 36) = p->flags;
    MR32(pd + 40) = p->cls <= 3 ? 4u * (elem >= 0 ? per : p->n) : 4u;
}

/* One ID3DXEffect method.  Returns 1 with *ret set if it was handled here. */
int fx_call(CPU *c, uint32_t self, unsigned m, uint32_t *ret) {
    uint32_t id = mem_is_committed(self + 4) ? MR32(self + 4) : 0;
    if (!id || id > (uint32_t)g_nfx) return 0;
    Fx *fx = &g_fx[id - 1];
    uint32_t eip = c->eip, r = 0;
    Ref ref; uint32_t *dst, n; FxParam *p;
    switch (m) {
    case 3:                                                   /* GetDesc */
        if (mem_is_committed(A(1) + 12)) { MR32(A(1)) = 0; MR32(A(1) + 4) = fx->nparams; MR32(A(1) + 8) = fx->ntechs; MR32(A(1) + 12) = 0; }
        break;
    case 4:                                                   /* GetParameterDesc */
        if (resolve(fx, A(1), &ref) && ref.p) desc_param(ref.p, A(2), ref.elem); else r = D3DERR_INVALIDCALL;
        break;
    case 5:                                                   /* GetTechniqueDesc */
        if (resolve(fx, A(1), &ref) && ref.t && mem_is_committed(A(2) + 8)) {
            MR32(A(2)) = ref.t->name_va; MR32(A(2) + 4) = ref.t->npasses; MR32(A(2) + 8) = ref.t->nann;
        } else r = D3DERR_INVALIDCALL;
        break;
    case 6:                                                   /* GetPassDesc */
        if (resolve(fx, A(1), &ref) && ref.ps && mem_is_committed(A(2) + 12)) {
            MR32(A(2)) = ref.ps->name_va; MR32(A(2) + 4) = ref.ps->nann;
            MR32(A(2) + 8) = ref.ps->vs ? ref.ps->vs->bytecode : 0;
            MR32(A(2) + 12) = ref.ps->ps ? ref.ps->ps->bytecode : 0;
        } else r = D3DERR_INVALIDCALL;
        break;
    case 7: r = D3DERR_INVALIDCALL; break;                    /* GetFunctionDesc: effects have none */
    case 8:                                                   /* GetParameter(parent, index) */
        r = (!A(1) && A(2) < fx->nparams) ? fx->params[A(2)].handle : 0;
        break;
    case 9:                                                   /* GetParameterByName(parent, name) */
        r = 0;
        if (A(2) && mem_is_committed(A(2)) && (p = param_by_name(fx, (const char *)g_mem + A(2), NULL))) r = p->handle;
        break;
    case 10:                                                  /* GetParameterBySemantic */
        r = 0;
        if (A(2) && mem_is_committed(A(2)))
            for (uint32_t i = 0; i < fx->nparams; i++)
                if (fx->params[i].sem[0] && !strcasecmp(fx->params[i].sem, (const char *)g_mem + A(2))) { r = fx->params[i].handle; break; }
        break;
    case 11:                                                  /* GetParameterElement(h, index) */
        r = 0;
        if (resolve(fx, A(1), &ref) && ref.kind == HK_PARAM && ref.p->elements && A(2) < ref.p->elements) {
            p = ref.p;
            pthread_mutex_lock(&g_fx_lock);
            if (!p->elem_handles) p->elem_handles = calloc(p->elements, 4);
            if (!p->elem_handles[A(2)])
                p->elem_handles[A(2)] = new_handle(HK_PARAM, (int)(fx - g_fx), (uint32_t)(p - fx->params), A(2), 0);
            r = p->elem_handles[A(2)];
            pthread_mutex_unlock(&g_fx_lock);
        }
        break;
    case 12: r = A(1) < fx->ntechs ? fx->techs[A(1)].handle : 0; break;          /* GetTechnique */
    case 13:                                                  /* GetTechniqueByName */
        r = 0;
        if (A(1) && mem_is_committed(A(1)))
            for (uint32_t i = 0; i < fx->ntechs; i++)
                if (!strcmp(fx->techs[i].name, (const char *)g_mem + A(1))) { r = fx->techs[i].handle; break; }
        break;
    case 14:                                                  /* GetPass(technique, index) */
        r = (resolve(fx, A(1), &ref) && ref.t && A(2) < ref.t->npasses) ? ref.t->passes[A(2)].handle : 0;
        break;
    case 15:                                                  /* GetPassByName */
        r = 0;
        if (resolve(fx, A(1), &ref) && ref.t && A(2) && mem_is_committed(A(2)))
            for (uint32_t i = 0; i < ref.t->npasses; i++)
                if (!strcmp(ref.t->passes[i].name, (const char *)g_mem + A(2))) { r = ref.t->passes[i].handle; break; }
        break;
    case 16: case 17: r = 0; break;                           /* GetFunction, GetFunctionByName */
    case 18: case 19: {                                       /* GetAnnotation(h, index), ...ByName(h, name) */
        FxParam *ann = NULL; uint32_t na = 0;
        r = 0;
        if (resolve(fx, A(1), &ref)) {
            if (ref.ps) { ann = ref.ps->ann; na = ref.ps->nann; }
            else if (ref.t) { ann = ref.t->ann; na = ref.t->nann; }
            else if (ref.p && ref.kind == HK_PARAM) { ann = ref.p->ann; na = ref.p->nann; }
        }
        if (m == 18) { if (A(2) < na) r = ann[A(2)].handle; }
        else if (A(2) && mem_is_committed(A(2)))
            for (uint32_t i = 0; i < na; i++)
                if (!strcmp(ann[i].name, (const char *)g_mem + A(2))) { r = ann[i].handle; break; }
        break;
    }
    case 20: case 21: case 78:                                /* SetValue, GetValue, SetRawValue */
        if ((p = num_param(fx, A(1), &dst, &n)) && A(2) && mem_is_committed(A(2))) {
            uint32_t off = m == 78 ? A(3) : 0, bytes = m == 78 ? A(4) : A(3);
            if (off > 4u * n) off = 4u * n;
            if (bytes > 4u * n - off) bytes = 4u * n - off;
            if (m == 78 && bytes == 64 && getenv("PROSTREET_FXLOG")) {
                const float *f = (const float *)(g_mem + A(2));
                fprintf(stderr, "fx: %s.%s raw matrix, caller %08x:", fx->name, p->name, MR32(c->esp + 4 * 5 + 0x0));
                for (int i = 0; i < 16; i++) fprintf(stderr, "%s%.3f", i % 4 ? " " : " | ", f[i]);
                fprintf(stderr, "\n");
            }
            if (m == 21) memcpy(g_mem + A(2), dst, bytes);
            else if (m == 78 && p->cls == 2 && p->rows == p->cols && !(off & 3)) {
                /* SetRawValue takes a matrix in the layout the shader's registers
                 * have (one column per register), without the transposition
                 * SetMatrix does: the game sets all its matrices this way.  Values
                 * are kept row-major here, so store the transpose. */
                uint32_t dim = p->cols, per = dim * dim;
                for (uint32_t i = 0; i + 4 <= bytes; i += 4) {
                    uint32_t j = (off + i) / 4, e = j / per, w = j % per;
                    dst[e * per + (w % dim) * dim + w / dim] = MR32(A(2) + i);
                }
            }
            else memcpy((uint8_t *)dst + off, g_mem + A(2), bytes);
        } else if (m != 78 && resolve(fx, A(1), &ref) && ref.p && ref.p->cls == 4 && A(2) && mem_is_committed(A(2))) {
            if (m == 20) ref.p->tex = MR32(A(2)); else MR32(A(2)) = ref.p->tex;   /* a texture as a value */
        } else r = D3DERR_INVALIDCALL;
        break;
    case 22: case 26: case 30:                                /* SetBool, SetInt, SetFloat */
        if ((p = num_param(fx, A(1), &dst, &n))) set_comp(p, dst, m == 22 ? 1 : m == 26 ? 2 : 3, A(2));
        else r = D3DERR_INVALIDCALL;
        break;
    case 23: case 27: case 31:                                /* GetBool, GetInt, GetFloat */
        if ((p = num_param(fx, A(1), &dst, &n))) OUT32(A(2), get_comp(p, dst[0], m == 23 ? 1 : m == 27 ? 2 : 3));
        else r = D3DERR_INVALIDCALL;
        break;
    case 24: case 28: case 32: case 25: case 29: case 33:     /* Set/Get Bool/Int/Float Array(h, p, count) */
        if ((p = num_param(fx, A(1), &dst, &n)) && A(2) && mem_is_committed(A(2))) {
            int kind = (m == 24 || m == 25) ? 1 : (m == 28 || m == 29) ? 2 : 3, get = m & 1;
            for (uint32_t i = 0; i < A(3) && i < n; i++) {
                if (get) MR32(A(2) + 4 * i) = get_comp(p, dst[i], kind);
                else set_comp(p, &dst[i], kind, MR32(A(2) + 4 * i));
            }
        } else r = D3DERR_INVALIDCALL;
        break;
    case 34: case 35: case 36: case 37:                       /* Set/GetVector, Set/GetVectorArray */
        if ((p = num_param(fx, A(1), &dst, &n)) && A(2) && mem_is_committed(A(2))) {
            uint32_t per = p->rows * p->cols, cnt = m >= 36 ? A(3) : 1, comps = per < 4 ? per : 4;
            for (uint32_t e = 0; e < cnt && (e + 1) * per <= n; e++)
                for (uint32_t q = 0; q < comps; q++) {
                    if (m & 1) MR32(A(2) + 16 * e + 4 * q) = get_comp(p, dst[e * per + q], 3);
                    else set_comp(p, &dst[e * per + q], 3, MR32(A(2) + 16 * e + 4 * q));
                }
        } else r = D3DERR_INVALIDCALL;
        break;
    case 38: case 39: case 40: case 41: case 42: case 43:     /* matrices; 44-49 the transposed forms */
    case 44: case 45: case 46: case 47: case 48: case 49:
        if ((p = num_param(fx, A(1), &dst, &n)) && A(2) && mem_is_committed(A(2))) {
            unsigned base = m >= 44 ? m - 44 : m - 38;        /* 0 set, 1 get, 2/3 array, 4/5 pointer array */
            int tr = m >= 44, get = base & 1;
            uint32_t per = p->rows * p->cols, cnt = base >= 2 ? A(3) : 1;
            for (uint32_t e = 0; e < cnt && (e + 1) * per <= n; e++) {
                uint32_t mp = base >= 4 ? MR32(A(2) + 4 * e) : A(2) + 64 * e;
                if (!mp || !mem_is_committed(mp + 60)) break;
                for (uint32_t row = 0; row < 4; row++) for (uint32_t col = 0; col < 4; col++) {
                    uint32_t g = mp + 4 * (tr ? col * 4 + row : row * 4 + col);
                    int in = row < p->rows && col < p->cols;
                    if (get) { float one = row == col ? 1.0f : 0.0f; uint32_t u; memcpy(&u, &one, 4);
                               MR32(g) = in ? get_comp(p, dst[e * per + row * p->cols + col], 3) : u; }
                    else if (in) set_comp(p, &dst[e * per + row * p->cols + col], 3, MR32(g));
                }
            }
        } else r = D3DERR_INVALIDCALL;
        break;
    case 50: break;                                           /* SetString */
    case 51: OUT32(A(2), 0); r = D3DERR_INVALIDCALL; break;   /* GetString: no string parameters */
    case 52:                                                  /* SetTexture */
        if (resolve(fx, A(1), &ref) && ref.p && ref.kind == HK_PARAM) ref.p->tex = A(2); else r = D3DERR_INVALIDCALL;
        break;
    case 53:                                                  /* GetTexture */
        if (resolve(fx, A(1), &ref) && ref.p) OUT32(A(2), ref.p->tex); else r = D3DERR_INVALIDCALL;
        break;
    case 54: case 55: OUT32(A(2), 0); break;                  /* GetPixelShader, GetVertexShader */
    case 56: break;                                           /* SetArrayRange */
    case 57: OUT32(A(1), 0); break;                           /* GetPool */
    case 58:                                                  /* SetTechnique */
        if (resolve(fx, A(1), &ref) && ref.t && !ref.ps) fx->cur_tech = (int)(ref.t - fx->techs); else r = D3DERR_INVALIDCALL;
        break;
    case 59: r = fx->ntechs ? fx->techs[fx->cur_tech].handle : 0; break;         /* GetCurrentTechnique */
    case 60: break;                                           /* ValidateTechnique */
    case 61: {                                                /* FindNextValidTechnique(h, *next) */
        uint32_t next = 0;
        if (!A(1)) next = fx->ntechs ? fx->techs[0].handle : 0;
        else if (resolve(fx, A(1), &ref) && ref.t && (uint32_t)(ref.t - fx->techs) + 1 < fx->ntechs) next = ref.t[1].handle;
        OUT32(A(2), next);
        r = next ? 0 : 1;                                     /* S_FALSE when there is none */
        break;
    }
    case 62: r = 1; break;                                    /* IsParameterUsed */
    case 63:                                                  /* Begin(*passes, flags) */
        OUT32(A(1), fx->ntechs ? fx->techs[fx->cur_tech].npasses : 0);
        fx->cur_pass = -1;
        break;
    case 64:                                                  /* BeginPass(index) */
        if (fx->ntechs && A(1) < fx->techs[fx->cur_tech].npasses) {
            fx->cur_pass = (int)A(1);
            apply_pass(c, fx, &fx->techs[fx->cur_tech].passes[fx->cur_pass], 1);
        } else r = D3DERR_INVALIDCALL;
        break;
    case 65:                                                  /* CommitChanges */
        if (fx->ntechs && fx->cur_pass >= 0) apply_pass(c, fx, &fx->techs[fx->cur_tech].passes[fx->cur_pass], 0);
        break;
    case 66: fx->cur_pass = -1; break;                        /* EndPass */
    case 67: break;                                           /* End */
    case 68: OUT32(A(1), com_get_device()); break;            /* GetDevice */
    case 69: case 70: break;                                  /* OnLostDevice, OnResetDevice */
    case 71: fx->manager = A(1); break;                       /* SetStateManager */
    case 72: OUT32(A(1), fx->manager); break;                 /* GetStateManager */
    case 73: case 75: case 76: break;                         /* parameter blocks: not used */
    case 74: r = 0; break;
    case 77: OUT32(A(2), 0); r = D3DERR_INVALIDCALL; break;   /* CloneEffect */
    default: return 0;
    }
    c->eip = eip;
    *ret = r;
    return 1;
}

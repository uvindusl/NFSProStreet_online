/* Win32 + MSVCR80 high-level emulation for the lifted ProStreet image.
 *
 * The image imports 371 symbols from 15 DLLs.  The IAT is bound to
 * IMP_MAGIC + index (see dispatch.c); every import call lands in
 * win32_call().  Functions implemented here run on the host; the rest are
 * logged stubs so the boot report shows exactly where the guest stopped. */
#define _GNU_SOURCE
#include "rt.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <wchar.h>

#ifndef O_BINARY
#define O_BINARY 0
#endif

/* ------------------------------------------------------------------ */
/* import bookkeeping                                                  */

static uint32_t *g_calls;      /* per-import call count */
static uint32_t *g_unimpl;     /* per-import unimplemented count */
static int g_trace;
static uint32_t g_trace_n;
static int g_unimpl_shown;

#define A(i) MR32(c->esp + 4u * (uint32_t)(i))

static const char *gs(uint32_t p) { return p ? (const char *)(g_mem + p) : ""; }

static void x87_push(CPU *c, double v) { FPUSH(v); }

static uint32_t gh_guest_str(const char *s) {
    size_t n = strlen(s) + 1;
    uint32_t p = gh_alloc((uint32_t)n, 1, 1);
    memcpy(g_mem + p, s, n);
    return p;
}

/* ------------------------------------------------------------------ */
/* guest heap: one bump arena, 16-byte aligned, header in front         */

#define HEAP_BASE 0x20000000u
#define HEAP_END  0x2C000000u
#define HEAP_MAGIC 0x48454150u

static uint32_t g_brk = HEAP_BASE;
static pthread_mutex_t g_heap_mtx = PTHREAD_MUTEX_INITIALIZER;

uint32_t gh_alloc(uint32_t n, int zero, uint32_t align) {
    if (n == 0) n = 1;
    if (align < 16) align = 16;
    pthread_mutex_lock(&g_heap_mtx);
    uint32_t p = (g_brk + 15u) & ~15u;
    uint32_t total = (n + 15u) & ~15u;
    if (p + 16u + total > HEAP_END) {
        fprintf(stderr, "heap: exhausted at %08x (asked %u)\n", g_brk, n);
        pthread_mutex_unlock(&g_heap_mtx);
        return 0;
    }
    g_brk = p + 16u + total;
    mem_commit(p, 16u + total);
    MR32(p) = HEAP_MAGIC;
    MR32(p + 4) = n;
    MR32(p + 8) = total;
    MR32(p + 12) = 0;
    uint32_t u = p + 16u;
    if (zero) memset(g_mem + u, 0, n);
    pthread_mutex_unlock(&g_heap_mtx);
    return u;
}

void gh_free(uint32_t p) {
    if (!p) return;
    if (MR32(p - 16u) != HEAP_MAGIC)
        fprintf(stderr, "heap: free of a bad pointer %08x\n", p);
    /* A real free list is not needed to reach the first milestone; the
     * arena is 192 MB.  Mark the block so a double free is visible. */
    if (MR32(p - 16u) == HEAP_MAGIC) MR32(p - 16u) = 0;
}

uint32_t gh_usize(uint32_t p) {
    if (!p || MR32(p - 16u) != HEAP_MAGIC) return 0;
    return MR32(p - 4);
}

/* Current top of the bump arena, for fault diagnostics. */
uint32_t gh_break(void) { return g_brk; }

uint32_t gh_realloc(uint32_t p, uint32_t n) {
    if (!p) return gh_alloc(n, 0, 16);
    if (!n) { gh_free(p); return 0; }
    uint32_t old = gh_usize(p);
    uint32_t q = gh_alloc(n, 0, 16);
    if (!q) return 0;
    memcpy(g_mem + q, g_mem + p, old < n ? old : n);
    gh_free(p);
    return q;
}

/* ------------------------------------------------------------------ */
/* virtual allocation (VirtualAlloc)                                    */

#define VA_BASE 0x30000000u
#define VA_END  0x38000000u
static uint32_t g_va_brk = VA_BASE;
static pthread_mutex_t g_va_mtx = PTHREAD_MUTEX_INITIALIZER;

static uint32_t va_alloc(uint32_t addr, uint32_t size) {
    size = (size + 0xFFFu) & ~0xFFFu;
    if (!size) size = 0x1000;
    pthread_mutex_lock(&g_va_mtx);
    if (!addr) {
        addr = (g_va_brk + 0xFFFu) & ~0xFFFu;
        if (addr + size > VA_END) { pthread_mutex_unlock(&g_va_mtx); return 0; }
        g_va_brk = addr + size;
    }
    mem_commit(addr, size);
    pthread_mutex_unlock(&g_va_mtx);
    return addr;
}

/* ------------------------------------------------------------------ */
/* guest TEB/PEB                                                        */

static uint32_t g_next_teb = 0x1C000000u;
static pthread_mutex_t g_teb_mtx = PTHREAD_MUTEX_INITIALIZER;

static uint32_t g_teb_tid = 1;
/* ------------------------------------------------------------------ */
/* per-thread state                                                    */
/* ------------------------------------------------------------------ */

/* fs:[0x2c] is ThreadLocalStoragePointer: a per-thread array of pointers
 * to each module's TLS block, indexed by that module's _tls_index.  The
 * image has one implicit-TLS module (index 0) with an 8-byte template
 * described by its PE TLS directory.  The guest reads
 *
 *   fs:[0x2c]  -> P
 *   P[0]       -> the image's TLS block Q
 *   Q[4]       -> a __declspec(thread) variable (the thread token)
 *
 * The Win32 TlsAlloc slots are a separate 64-DWORD array at TEB+0xE10. */
#define TEB_TLS_SLOTS   0x000E10u    /* TlsSlots[64]                  */
#define TEB_TLS_SLOTS_N 64u
#define TEB_TLS_PTR     0x0002C0u    /* ThreadLocalStoragePointer     */
#define TEB_TLS_BLOCK   0x000300u    /* this image's module 0 block   */
#define TEB_TLS_PTR_N   16u          /* room for P[] before the block */
#define IMAGE_TLS_DIR   0x009FB418u  /* PE TLS directory, VA in g_mem */

void teb_tls_init(uint32_t teb) {
    uint32_t start = MR32(IMAGE_TLS_DIR + 0);
    uint32_t end   = MR32(IMAGE_TLS_DIR + 4);
    uint32_t idxva = MR32(IMAGE_TLS_DIR + 8);
    uint32_t zero  = MR32(IMAGE_TLS_DIR + 16);
    uint32_t size  = end > start ? end - start : 0;
    uint32_t index = (idxva && mem_is_committed(idxva)) ? MR32(idxva) : 0;
    uint32_t ptr   = teb + TEB_TLS_PTR;
    uint32_t blk   = teb + TEB_TLS_BLOCK;

    if (size > TEB_TLS_BLOCK - TEB_TLS_PTR) size = TEB_TLS_BLOCK - TEB_TLS_PTR;
    if (zero > 0x400u) zero = 0x400u;

    /* Copy the module's TLS template, then zero the zero-fill tail. */
    for (uint32_t i = 0; i < size; i++) MR8(blk + i) = MR8(start + i);
    for (uint32_t i = 0; i < zero; i++) MR8(blk + size + i) = 0;

    if (index >= TEB_TLS_PTR_N) index = 0;
    MR32(ptr + 4u * index) = blk;
    MR32(teb + 0x2C) = ptr;          /* fs:[0x2c] */
    for (uint32_t i = 0; i < TEB_TLS_SLOTS_N; i++)
        MR32(teb + TEB_TLS_SLOTS + 4u * i) = 0;
}

/* The TlsAlloc index space is process wide, the slot values are per
 * thread - exactly like Windows. */
static uint32_t g_tls_next_index;

static uint32_t tls_slot(CPU *c, uint32_t idx) {
    if (!c->fs_base || idx >= TEB_TLS_SLOTS_N) return 0;
    return c->fs_base + TEB_TLS_SLOTS + 4u * idx;
}

static uint32_t new_teb(uint32_t stack_lo, uint32_t stack_hi) {
    pthread_mutex_lock(&g_teb_mtx);
    uint32_t t = g_next_teb;
    g_next_teb += 0x10000u;
    pthread_mutex_unlock(&g_teb_mtx);
    mem_commit(t, 0x10000u);
    memset(g_mem + t, 0, 0x10000u);
    MR32(t + 0x00) = 0xFFFFFFFFu;
    MR32(t + 0x04) = stack_hi;
    MR32(t + 0x08) = stack_lo;
    MR32(t + 0x18) = t;
    MR32(t + 0x20) = (uint32_t)getpid();
    MR32(t + 0x24) = g_teb_tid++;
    MR32(t + 0x30) = PEB_BASE;
    teb_tls_init(t);
    return t;
}

/* ------------------------------------------------------------------ */
/* handles: threads, events, mutexes, semaphores                        */

enum { HK_THREAD = 1, HK_EVENT = 2, HK_MUTEX = 3, HK_SEM = 4 };
typedef struct {
    int used, kind, manual, signaled, done, owns;
    long count;
    pthread_t th;
    uint32_t code, teb, stack_lo, stack_hi;
    uint32_t fn_unused, arg_unused;
    pthread_mutex_t m;
    pthread_cond_t cv;
} HEnt;
static HEnt g_h[64];
static pthread_mutex_t g_h_mtx = PTHREAD_MUTEX_INITIALIZER;

static uint32_t h_make(int kind) {
    pthread_mutex_lock(&g_h_mtx);
    for (int i = 0; i < 64; i++) {
        if (g_h[i].used) continue;
        memset(&g_h[i], 0, sizeof g_h[i]);
        g_h[i].used = 1;
        g_h[i].kind = kind;
        pthread_mutex_init(&g_h[i].m, 0);
        pthread_cond_init(&g_h[i].cv, 0);
        pthread_mutex_unlock(&g_h_mtx);
        return 0x60000000u | ((uint32_t)i << 4) | (uint32_t)kind;
    }
    pthread_mutex_unlock(&g_h_mtx);
    return 0;
}
static HEnt *h_get(uint32_t h) {
    if ((h & 0xF0000000u) != 0x60000000u) return 0;
    uint32_t i = (h >> 4) & 0x3F;
    if (!g_h[i].used) return 0;
    return &g_h[i];
}

static void *guest_thread(void *arg) {
    int i = (int)(intptr_t)arg;
    HEnt *h = &g_h[i];
    CPU *c = calloc(1, sizeof *c);
    if (!c) return 0;
    /* Set up guest CPU context */
    c->fs_base = h->teb;              /* fs: -> per-thread TEB   */
    c->gs_base = h->teb;              /* gs: -> per-thread TEB   */
    c->esp = h->stack_hi - 0x1000u;   /* guest stack top         */
    c->fcw = 0x027f;                  /* x87 control word        */
    c->ebp = c->esp;
    c->ebx = 0;
    c->esi = 0;
    c->edi = 0;
    g_cur_cpu = c;                    /* thread-local (see rt.h) */

    /* Diagnostics: the fault that started this investigation happened with
     * fs_base == 0 in the CPU that the lifted code was reading.  Print the
     * whole chain so a regression is immediately visible. */
    fprintf(stderr, "  [thread] enter i=%d h=%p h->teb=%08x c=%p c->fs_base=%08x "
                    "c->gs_base=%08x esp=%08x arg=%08x start=%08x\n",
            i, (void *)h, h->teb, (void *)c, c->fs_base, c->gs_base, c->esp,
            h->arg_unused, h->fn_unused);

    /* Call the guest thread procedure with the stdcall convention the
     * CRT expects: the argument sits at [esp+4] and the return address
     * at [esp], so the argument must be pushed *before* the sentinel.
     * (Pushing them the other way round makes the callee read the
     * sentinel 0xFFFFFFF0 as its argument, and its first dereference
     *  [esi+0x10] then wraps to guest address 0 -> SIGSEGV.) */
    PUSH32(h->arg_unused);
    PUSH32(SENTINEL);
    dispatch_indirect(c, h->fn_unused);

    /* Follow tail jumps until the guest returns to the sentinel. */
    uint32_t guard = 0;
    while (c->eip != SENTINEL) {
        uint32_t t = c->eip;
        c->eip = 0;
        if (++guard > 1000000u) break;
        dispatch_indirect(c, t);
    }

    h->owns = 1;
    pthread_mutex_lock(&h->m);
    h->code = c->eax;
    h->done = 1;
    pthread_cond_broadcast(&h->cv);
    pthread_mutex_unlock(&h->m);
    free(c);
    return 0;
}

/* ------------------------------------------------------------------ */
/* guest file handles                                                   */

#define IOB_BASE 0x1B020000u
#define IOB_STRIDE 128u
#define FH_BASE 0x50000000u

typedef struct { int used; FILE *fp; } FileEnt;
static FileEnt g_files[64];

static FILE *file_of(uint32_t h) {
    if (h >= IOB_BASE && h < IOB_BASE + 3 * IOB_STRIDE) {
        uint32_t i = (h - IOB_BASE) / IOB_STRIDE;
        return i == 0 ? stdin : i == 1 ? stdout : stderr;
    }
    if ((h & 0xF0000000u) != FH_BASE) return 0;
    uint32_t i = (h >> 4) & 0x3F;
    return g_files[i].used ? g_files[i].fp : 0;
}
static uint32_t file_new(FILE *fp) {
    for (int i = 0; i < 64; i++)
        if (!g_files[i].used) {
            g_files[i].used = 1;
            g_files[i].fp = fp;
            return FH_BASE | ((uint32_t)i << 4);
        }
    return 0;
}

static const char *g_gamedir = "game";
static char g_pathbuf[4][4096];
static int g_pathi;

static const char *xp(const char *p) {
    char *b = g_pathbuf[g_pathi++ & 3];
    if (!p) p = "";
    if (p[0] == '/' || (isalpha((unsigned char)p[0]) && p[1] == ':')) {
        if (p[0] && p[1] == ':') {
            /* C:\foo -> /foo */
            snprintf(b, 4096, "%s", p + 2);
            for (char *q = b; *q; q++) if (*q == '\\') *q = '/';
        } else {
            snprintf(b, 4096, "%s", p);
        }
        return b;
    }
    snprintf(b, 4096, "%s/%s", g_gamedir, p);
    for (char *q = b; *q; q++) if (*q == '\\') *q = '/';
    return b;
}

/* ------------------------------------------------------------------ */
/* guest printf-family formatter                                        */

static int gfmt(char *dst, size_t cap, const char *fmt, uint32_t *base) {
    size_t o = 0;
    int ai = 0;
#define PUT(s, n) do { size_t _n = (n); if (o + _n >= cap) _n = cap > o ? cap - o - 1 : 0; \
    if (_n) { memcpy(dst + o, (s), _n); o += _n; } } while (0)
    while (*fmt) {
        if (*fmt != '%') {
            const char *q = fmt;
            while (*q && *q != '%') q++;
            PUT(fmt, (size_t)(q - fmt));
            fmt = q;
            continue;
        }
        if (fmt[1] == '%') { PUT("%", 1); fmt += 2; continue; }
        const char *st = fmt++;
        char spec[32];
        size_t sl = 0;
        spec[sl++] = '%';
        while (*fmt && strchr("-+ #0", *fmt) && sl < 28) spec[sl++] = *fmt++;
        while (isdigit((unsigned char)*fmt) && sl < 28) spec[sl++] = *fmt++;
        if (*fmt == '*') { fmt++; }  /* width from arg: unsupported */
        if (*fmt == '.') {
            spec[sl++] = *fmt++;
            if (*fmt == '*') fmt++;
            while (isdigit((unsigned char)*fmt) && sl < 28) spec[sl++] = *fmt++;
        }
        int lng = 0;
        if (fmt[0] == 'I' && fmt[1] == '6' && fmt[2] == '4') { lng = 2; fmt += 3; }
        else if (fmt[0] == 'l' && fmt[1] == 'l') { lng = 2; fmt += 2; }
        else if (fmt[0] == 'l') { lng = 1; fmt++; }
        else if (fmt[0] == 'h' && fmt[1] == 'h') { lng = -2; fmt += 2; }
        else if (fmt[0] == 'h') { lng = -1; fmt++; }
        char conv = *fmt ? *fmt++ : 0;
        char tmp[512];
        int n = 0;
        switch (conv) {
        case 'd': case 'i': {
            if (lng == 2) { long long v = (long long)((uint64_t)base[ai] | ((uint64_t)base[ai + 1] << 32)); ai += 2;
                spec[sl] = 0; strcat(spec, "lld"); n = snprintf(tmp, sizeof tmp, spec, v); }
            else { int v = (int)base[ai++]; spec[sl] = 0; strcat(spec, "d"); n = snprintf(tmp, sizeof tmp, spec, v); }
            break; }
        case 'u': case 'x': case 'X': case 'o': {
            if (lng == 2) { unsigned long long v = (unsigned long long)base[ai] | ((unsigned long long)base[ai + 1] << 32); ai += 2;
                spec[sl] = 0; strcat(spec, "ll"); spec[sl + 2] = conv; spec[sl + 3] = 0; n = snprintf(tmp, sizeof tmp, spec, v); }
            else { unsigned v = base[ai++]; spec[sl] = 0; spec[sl + 1] = conv; spec[sl + 2] = 0; n = snprintf(tmp, sizeof tmp, spec, v); }
            break; }
        case 'c': { int v = (int)base[ai++]; spec[sl] = 0; strcat(spec, "c"); n = snprintf(tmp, sizeof tmp, spec, v); break; }
        case 's': { uint32_t p = base[ai++]; spec[sl] = 0; strcat(spec, "s");
            n = snprintf(tmp, sizeof tmp, spec, p ? (const char *)(g_mem + p) : "(null)"); break; }
        case 'p': { uint32_t p = base[ai++]; spec[sl] = 0; strcat(spec, "08x"); n = snprintf(tmp, sizeof tmp, spec, p); break; }
        case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': {
            uint64_t bits = (uint64_t)base[ai] | ((uint64_t)base[ai + 1] << 32); ai += 2;
            double v; memcpy(&v, &bits, 8);
            spec[sl] = 0; spec[sl + 1] = conv; spec[sl + 2] = 0;
            n = snprintf(tmp, sizeof tmp, spec, v); break; }
        case 'n': ai++; break;
        default: tmp[0] = '%'; tmp[1] = conv; tmp[2] = 0; n = 2; break;
        }
        if (n > 0) PUT(tmp, (size_t)n);
        (void)st;
    }
    if (cap) dst[o < cap ? o : cap - 1] = 0;
    return (int)o;
#undef PUT
}

/* ------------------------------------------------------------------ */
/* CRT globals that the lifted startup code writes through              */

#define CR_FMODE   0x1B002010u
#define CR_COMMODE 0x1B002014u
#define CR_INITENV 0x1B002020u
#define CR_ADJF    0x1B002030u
#define CR_ADJFV   0x1B002034u

static uint32_t g_cmdline_va;

/* ------------------------------------------------------------------ */
/* time helpers                                                         */

static uint64_t real_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ull + (uint64_t)ts.tv_nsec / 1000ull;
}
static uint64_t real_ms(void) { return real_us() / 1000ull; }

/* ------------------------------------------------------------------ */
/* thread start                                                         */

static uint32_t thread_create(uint32_t start, uint32_t arg, int beginthr) {
    uint32_t h = h_make(HK_THREAD);
    if (!h) return 0;
    HEnt *e = h_get(h);
    uint32_t lo = 0x70000000u + ((h >> 4) & 0x3F) * 0x100000u;
    uint32_t hi = lo + 0x100000u;
    mem_commit(lo, hi - lo);
    e->stack_lo = lo;
    e->stack_hi = hi;
    e->teb = new_teb(lo, hi);
    e->fn_unused = start;
    e->arg_unused = arg;
    e->owns = beginthr;
    /* Diagnostics: verify the handle -> index -> HEnt mapping is consistent
     * and that the guest thread argument is what the CRT passed in. */
    {
        HEnt *chk = h_get(h);
        fprintf(stderr, "  [thread] handle=%08x idx=%u start=%08x arg=%08x "
                        "teb=%08x stack=%08x-%08x h_get=%p g_h[idx]=%p %s\n",
                h, (unsigned)((h >> 4) & 0x3F), start, arg, e->teb, lo, hi,
                (void *)chk, (void *)&g_h[(h >> 4) & 0x3F],
                chk == &g_h[(h >> 4) & 0x3F] ? "ok" : "MISMATCH");
    }
    pthread_attr_t at;
    pthread_attr_init(&at);
    pthread_attr_setstacksize(&at, 64ull * 1024ull * 1024ull);
    pthread_create(&e->th, &at, guest_thread, (void *)(intptr_t)((h >> 4) & 0x3F));
    pthread_attr_destroy(&at);
    return h;
}

/* ------------------------------------------------------------------ */
/* the dispatcher                                                       */

/* ------------------------------------------------------------------ */
/* Windows structured exception handling                                */
/* ------------------------------------------------------------------ */

/* The guest uses MSVC's x86 EH ABI in five functions.  A function that owns
 * a __try block pushes a registration record and points fs:[0] at it:
 *
 *   frame+0x00  the previous registration record (the fs:[0] chain)
 *   frame+0x04  the establisher routine (0x9204d8 -> MSVCR80!_except_handler3)
 *   frame+0x08  the scope table
 *   frame+0x0c  the try level (-1 when no __try block is active)
 *   frame+0x10  the owning function's EBP
 *   frame-0x04  EXCEPTION_POINTERS*, handed to the filter expression
 *
 * The scope table holds 12-byte entries:
 *   +0x00 the enclosing try level, +0x04 the filter, +0x08 the handler
 *
 * MSVCR80's _except_handler3 calls the filter with EBP = frame+0x10 and every
 * other register zeroed, and reads the answer as
 *   -1  EXCEPTION_CONTINUE_EXECUTION -> resume at the raise point
 *    0  EXCEPTION_CONTINUE_SEARCH    -> try the enclosing level / outer frame
 *    1  EXCEPTION_EXECUTE_HANDLER    -> run the handler
 * and answers the OS with an EXCEPTION_DISPOSITION:
 *    0  ExceptionContinueExecution, 1  ExceptionContinueSearch,
 *    2  ExceptionExecuteHandler
 */

#define SEH_FRAME_PREV   0x00u
#define SEH_FRAME_HDL    0x04u
#define SEH_FRAME_SCOPE  0x08u
#define SEH_FRAME_LEVEL  0x0cu
#define SEH_FRAME_EBP    0x10u
#define SEH_PTRS_FRAME   (-4)         /* frame-4: EXCEPTION_POINTERS* */

/* EXCEPTION_DISPOSITION, the answer the establisher gives the OS. */
#define SEH_DISP_CONTINUE_EXECUTION 0u
#define SEH_DISP_CONTINUE_SEARCH    1u
#define SEH_DISP_EXECUTE_HANDLER    2u

/* The guest's scope tables use absolute filter/handler addresses and a
 * 12-byte stride (MSVC's _except_handler3 computes esi*3*4). */
#define SEH_SCOPE_STRIDE 12u

/* Return marker for the calls the SEH machinery makes into guest code.  It
 * differs from SENTINEL so that a handler which transfers control somewhere
 * else (an MSVC handler ends with "mov esp,[ebp-18] ... ret", which returns
 * from the function that owns the try block) can be told apart from a
 * handler that simply returned. */
#define SEH_MARKER 0xFFFFFFF1u

/* Scratch space in guest memory: EXCEPTION_RECORD, CONTEXT, the
 * EXCEPTION_POINTERS pair and the dispatcher context. */
#define SEH_EXCREC_OFF 0x000u
#define SEH_CTX_OFF    0x060u
#define SEH_PTRS_OFF   0x320u
#define SEH_DCTX_OFF   0x330u
#define SEH_SCRATCH_SZ 0x340u

static __thread uint32_t g_seh_scratch;   /* per-thread, in guest memory */

static uint32_t seh_scratch(void) {
    if (!g_seh_scratch) g_seh_scratch = gh_alloc(SEH_SCRATCH_SZ, 1, 16);
    return g_seh_scratch;
}

/* x86 EXCEPTION_RECORD: code, flags, nested record, address, parameter count
 * and up to 15 parameters. */
static void seh_fill_record(uint32_t base, uint32_t code, uint32_t flags,
                            uint32_t nargs, uint32_t argptr, uint32_t eip) {
    memset(g_mem + base, 0, 0x54u);
    MR32(base + 0x00) = code;
    MR32(base + 0x04) = flags;
    MR32(base + 0x08) = 0;                    /* no nested exception record */
    MR32(base + 0x0c) = 0;
    MR32(base + 0x10) = eip;                  /* ExceptionAddress */
    uint32_t n = nargs > 15u ? 15u : nargs;
    MR32(base + 0x14) = n;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t src = argptr + 4u * i;
        MR32(base + 0x18u + 4u * i) =
            (src && mem_is_committed(src)) ? MR32(src) : 0u;
    }
}

/* x86 CONTEXT (CONTEXT_FULL). */
static void seh_fill_context(CPU *c, uint32_t base, uint32_t eip) {
    memset(g_mem + base, 0, 0x2B8u);
    MR32(base + 0x000) = 0x00010007u;         /* CONTEXT_FULL */
    MR16(base + 0x08c) = 0;                   /* SegGs */
    MR16(base + 0x08e) = 0;                   /* SegFs */
    MR16(base + 0x090) = 0;                   /* SegEs */
    MR16(base + 0x092) = 0;                   /* SegDs */
    MR32(base + 0x094) = c->edi;
    MR32(base + 0x098) = c->esi;
    MR32(base + 0x09c) = c->ebx;
    MR32(base + 0x0a0) = c->esp;
    MR32(base + 0x0a4) = c->ebp;
    MR32(base + 0x0a8) = eip;
    MR16(base + 0x0ac) = 0x0023u;             /* SegCs */
    MR16(base + 0x0ae) = (uint16_t)eflags_get(c);
    MR32(base + 0x0b0) = c->esp;
    MR32(base + 0x0b4) = 0x0023u;             /* Ss */
}

/* Call guest code for the SEH machinery.  Unlike guest_call() this does not
 * follow tail jumps: a handler may transfer control somewhere else entirely,
 * and the caller has to notice that.  Returns 1 when the callee returned
 * normally, 0 when it transferred control elsewhere. */
static int seh_invoke(CPU *c, uint32_t fn) {
    PUSH32(SEH_MARKER);
    dispatch_indirect(c, fn);
    if (c->eip == SEH_MARKER) return 1;
    if (mem_is_committed(c->esp) && MR32(c->esp) == SEH_MARKER) {
        /* the callee tail-jumped: the marker is still on the stack */
        uint32_t guard = 0;
        while (c->eip != SEH_MARKER) {
            uint32_t t = c->eip;
            c->eip = 0;
            if (++guard > 1000000u) break;
            dispatch_indirect(c, t);
            if (c->eip == SEH_MARKER) return 1;
            if (!(mem_is_committed(c->esp) && MR32(c->esp) == SEH_MARKER)) return 0;
        }
        return 1;
    }
    return 0;
}

/* Call the guest's filter expression with MSVC's x86 EH convention: EBP is
 * the establisher frame's function EBP, every other register is zeroed and
 * the stack holds the try level and the establisher's own frame pointer, as
 * MSVCR80's _except_handler3 sets them up.  Returns the filter's answer. */
static int seh_call_filter(CPU *c, uint32_t fn, uint32_t frame, uint32_t level) {
    uint32_t s_esp = c->esp, s_ebp = c->ebp, s_eax = c->eax, s_ebx = c->ebx;
    uint32_t s_ecx = c->ecx, s_edx = c->edx, s_esi = c->esi, s_edi = c->edi;
    uint32_t s_eip = c->eip, s_fl = eflags_get(c);

    c->ebp = frame + SEH_FRAME_EBP;
    c->ebx = c->ecx = c->edx = c->esi = c->edi = 0;
    PUSH32(level);
    PUSH32(s_ebp);
    seh_invoke(c, fn);
    int r = (int)c->eax;

    c->esp = s_esp; c->ebp = s_ebp; c->eax = s_eax; c->ebx = s_ebx;
    c->ecx = s_ecx; c->edx = s_edx; c->esi = s_esi; c->edi = s_edi;
    c->eip = s_eip;
    eflags_set(c, s_fl);
    return r;
}

/* Call the guest's handler with MSVC's x86 EH convention.  Returns 1 when
 * the handler returned normally and 0 when it transferred control elsewhere
 * (which is what MSVC's handlers do: they restore the stack of the function
 * that owns the try block and return from it). */
static int seh_call_handler(CPU *c, uint32_t fn, uint32_t frame) {
    uint32_t s_esp = c->esp, s_ebp = c->ebp, s_eax = c->eax, s_ebx = c->ebx;
    uint32_t s_ecx = c->ecx, s_edx = c->edx, s_esi = c->esi, s_edi = c->edi;
    uint32_t s_eip = c->eip, s_fl = eflags_get(c);

    c->ebp = frame + SEH_FRAME_EBP;
    c->ebx = c->ecx = c->edx = c->esi = c->edi = 0;
    int returned = seh_invoke(c, fn);
    if (returned) {
        c->esp = s_esp; c->ebp = s_ebp; c->eax = s_eax; c->ebx = s_ebx;
        c->ecx = s_ecx; c->edx = s_edx; c->esi = s_esi; c->edi = s_edi;
        c->eip = s_eip;
        eflags_set(c, s_fl);
    }
    return returned;
}

/* MSVCR80's _except_handler3, the establisher the game installs.  It walks
 * the scope table of the frame, calls the filter of the active try level and
 * acts on the answer.  The caller owns the exception arguments; the guest
 * stack must be clean here (the filter and the handler get the stack exactly
 * as MSVCR80 gives it). */
static uint32_t seh_msvc3(CPU *c, uint32_t excrec, uint32_t frame,
                          uint32_t ctx, uint32_t dctx) {
    uint32_t level = MR32(frame + SEH_FRAME_LEVEL);
    uint32_t scope = MR32(frame + SEH_FRAME_SCOPE);
    uint32_t ptrs  = seh_scratch() + SEH_PTRS_OFF;

    /* frame-4 = &EXCEPTION_POINTERS {ExceptionRecord, ContextRecord} */
    MR32(ptrs + 0) = excrec;
    MR32(ptrs + 4) = ctx;
    MR32(frame + (uint32_t)SEH_PTRS_FRAME) = ptrs;

    for (;;) {
        if (level == 0xFFFFFFFFu) return SEH_DISP_CONTINUE_SEARCH;
        if (!mem_is_committed(scope + level * SEH_SCOPE_STRIDE + 8u))
            return SEH_DISP_CONTINUE_SEARCH;
        uint32_t entry   = scope + level * SEH_SCOPE_STRIDE;
        uint32_t filter  = MR32(entry + 4);
        uint32_t handler = MR32(entry + 8);

        if (!filter) {                       /* no filter: unwind one level */
            level = MR32(entry);
            continue;
        }
        int r = seh_call_filter(c, filter, frame, level);
        if (r == 0) {                        /* CONTINUE_SEARCH: outer level */
            level = MR32(entry);
            continue;
        }
        if (r < 0) return SEH_DISP_CONTINUE_EXECUTION;

        /* EXCEPTION_EXECUTE_HANDLER: run the handler, then the frame's try
         * level falls back to the enclosing one. */
        MR32(frame + SEH_FRAME_LEVEL) = MR32(entry);
        (void)seh_call_handler(c, handler, frame);
        return SEH_DISP_CONTINUE_EXECUTION;
    }
}

/* Give an exception to the guest's SEH chain the way the Windows dispatcher
 * does: walk fs:[0] and let every registration record's establisher have a
 * look at it.  Returns 1 when a handler took the exception. */
static int seh_dispatch(CPU *c, uint32_t code, uint32_t flags,
                        uint32_t nargs, uint32_t argptr) {
    uint32_t sc     = seh_scratch();
    uint32_t excrec = sc + SEH_EXCREC_OFF;
    uint32_t ctx    = sc + SEH_CTX_OFF;
    uint32_t dctx   = sc + SEH_DCTX_OFF;

    seh_fill_record(excrec, code, flags, nargs, argptr, c->eip);
    seh_fill_context(c, ctx, c->eip);
    MR32(dctx) = 0;

    uint32_t frame = c->fs_base ? MR32(c->fs_base) : 0xFFFFFFFFu;
    int guard = 0;
    while (frame != 0xFFFFFFFFu && frame != 0u) {
        if (++guard > 256) break;
        if (!mem_is_committed(frame + SEH_FRAME_EBP)) break;
        uint32_t handler = MR32(frame + SEH_FRAME_HDL);

        /* The OS calls the establisher with (ExceptionRecord,
         * EstablisherFrame, ContextRecord, DispatcherContext).  The game's
         * establisher is the MSVCR80 thunk, which routes to our native
         * _except_handler3. */
        uint32_t s_esp = c->esp;
        PUSH32(dctx);
        PUSH32(ctx);
        PUSH32(frame);
        PUSH32(excrec);
        guest_call(c, handler);
        c->esp = s_esp;
        uint32_t disp = c->eax;

        if (disp == SEH_DISP_CONTINUE_EXECUTION || disp == SEH_DISP_EXECUTE_HANDLER)
            return 1;
        frame = MR32(frame + SEH_FRAME_PREV);
    }
    return 0;
}

static int str_eq(const char *a, const char *b) { return !strcmp(a, b); }

int win32_call(CPU *c, uint32_t idx) {
    if (idx >= g_imp_count) return 0;
    const ImpEnt *e = &g_imp[idx];
    const char *n = e->name;

    POP32(c->eip);                 /* the caller's return address */
    g_calls[idx]++;
    if (g_trace && g_trace_n < 20000) {
        fprintf(stderr, "  [%5u] %-14s %-40s ret=%08x a0=%08x a1=%08x\n",
                ++g_trace_n, e->dll, n, c->eip,
                A(0), A(1));
    }

    uint32_t r = 0;
    int handled = 1;

    /* ---- CRT startup ------------------------------------------------ */
    if (str_eq(n, "__set_app_type")) {
        r = 0;
    } else if (str_eq(n, "__setusermatherr")) {
        r = 0;
    } else if (str_eq(n, "_configthreadlocale")) {
        r = 0;
    } else if (str_eq(n, "_encode_pointer") || str_eq(n, "_decode_pointer")) {
        r = A(0);
    } else if (str_eq(n, "__p__fmode")) {
        r = CR_FMODE;
    } else if (str_eq(n, "__p__commode")) {
        r = CR_COMMODE;
    } else if (str_eq(n, "_initterm") || str_eq(n, "_initterm_e")) {
        uint32_t begin = A(0), end = A(1);
        uint32_t cnt = 0;
        uint32_t slots = (end > begin) ? (end - begin) / 4u : 0;
        fprintf(stderr, "  %s(%08x,%08x): %u slots\n", n, begin, end, slots);
        for (uint32_t i = 0, p = begin; p < end; p += 4, i++) {
            uint32_t fn = MR32(p);
            if (!fn) continue;
            cnt++;
            fprintf(stderr, "    %s[%u] ptr=%08x fn=%08x ...\n", n, i, p, fn);
            if (!guest_call(c, fn)) {
                fprintf(stderr, "    %s[%u] fn=%08x FAILED (guest_call returned 0)\n", n, i, fn);
                break;
            }
            fprintf(stderr, "    %s[%u] fn=%08x ok\n", n, i, fn);
        }
        fprintf(stderr, "  %s(%08x,%08x): %u/%u constructors ran\n", n, begin, end, cnt, slots);
        r = 0;
    } else if (str_eq(n, "__getmainargs")) {
        uint32_t argc = A(0), argv = A(1), envp = A(2), si = A(4);
        static const char *nm = "nfsps.exe";
        uint32_t s = gh_guest_str(nm);
        uint32_t av = gh_alloc(8, 1, 4);
        MR32(av) = s; MR32(av + 4) = 0;
        uint32_t ev = gh_alloc(4, 1, 4);
        MR32(ev) = 0;
        if (argc) MR32(argc) = 1;
        if (argv) MR32(argv) = av;
        if (envp) MR32(envp) = ev;
        if (si) { mem_commit(si, 0x40); MR32(si) = 0; }   /* newmode = 0 */
        fprintf(stderr, "  __getmainargs: argc=1 argv=%08x\n", av);
        r = 0;
    } else if (str_eq(n, "_amsg_exit")) {
        fprintf(stderr, "\n*** _amsg_exit(%u)\n", A(0));
        guest_exit(255);
    } else if (str_eq(n, "_XcptFilter")) {
        r = 0;
    } else if (str_eq(n, "_except_handler4_common")) {
        /* MSVC's x64 / IA64 establisher; the game never installs it. */
        r = 1;  /* ExceptionContinueSearch */
    } else if (str_eq(n, "__CxxFrameHandler3")) {
        r = 1;
    } else if (str_eq(n, "_crt_debugger_hook")) {
        r = 0;
    } else if (str_eq(n, "_invoke_watson") || str_eq(n, "_purecall")) {
        fprintf(stderr, "\n*** %s called\n", n);
        guest_exit(255);
    } else if (str_eq(n, "_invalid_parameter_noinfo")) {
        r = 0;
    } else if (str_eq(n, "_controlfp_s") || str_eq(n, "_control87") || str_eq(n, "_controlfp")) {
        r = 0;
    } else if (str_eq(n, "_onexit") || str_eq(n, "__dllonexit")) {
        r = A(0);
    } else if (str_eq(n, "_lock") || str_eq(n, "_unlock")) {
        r = 0;
    } else if (str_eq(n, "?terminate@@YAXXZ")) {
        fprintf(stderr, "\n*** std::terminate\n");
        guest_exit(255);
    } else if (str_eq(n, "exit") || str_eq(n, "_exit") || str_eq(n, "_cexit") || str_eq(n, "_c_exit")) {
        int code = (int)A(0);
        fprintf(stderr, "\n*** guest %s(%d)\n", n, code);
        guest_exit(code);
    } else if (str_eq(n, "TerminateProcess") || str_eq(n, "ExitProcess")) {
        fprintf(stderr, "\n*** guest %s(%d)\n", n, (int)A(1));
        guest_exit((int)A(1));

    /* ---- heap -------------------------------------------------------- */
    } else if (str_eq(n, "GetProcessHeap")) {
        r = 0x00010001u;
    } else if (str_eq(n, "HeapCreate")) {
        r = 0x00010002u;
    } else if (str_eq(n, "HeapDestroy")) {
        r = 1;
    } else if (str_eq(n, "HeapAlloc")) {
        r = gh_alloc(A(2), (A(1) & 8u) != 0, 16);
    } else if (str_eq(n, "HeapFree")) {
        gh_free(A(2));
        r = 1;
    } else if (str_eq(n, "HeapReAlloc")) {
        r = gh_realloc(A(2), A(3));
    } else if (str_eq(n, "HeapSize")) {
        r = gh_usize(A(2));
    } else if (str_eq(n, "VirtualAlloc")) {
        r = va_alloc(A(0), A(1));
    } else if (str_eq(n, "VirtualFree")) {
        r = 1;
    } else if (str_eq(n, "VirtualProtect")) {
        if (A(3)) MR32(A(3)) = 0x40;
        r = 1;

    /* ---- memory / strings (cdecl) ----------------------------------- */
    } else if (str_eq(n, "malloc")) {
        r = gh_alloc(A(0), 0, 16);
    } else if (str_eq(n, "calloc")) {
        uint32_t cnt = A(0), sz = A(1);
        r = gh_alloc(cnt * sz, 1, 16);
    } else if (str_eq(n, "free")) {
        gh_free(A(0));
        r = 0;
    } else if (str_eq(n, "realloc")) {
        r = gh_realloc(A(0), A(1));
    } else if (str_eq(n, "_aligned_malloc")) {
        r = gh_alloc(A(0), 0, A(1));
    } else if (str_eq(n, "_aligned_free")) {
        gh_free(A(0));
        r = 0;
    } else if (str_eq(n, "memcpy")) {
        memcpy(g_mem + A(0), g_mem + A(1), A(2)); r = A(0);
    } else if (str_eq(n, "memmove")) {
        memmove(g_mem + A(0), g_mem + A(1), A(2)); r = A(0);
    } else if (str_eq(n, "memset")) {
        memset(g_mem + A(0), (int)A(1), A(2)); r = A(0);
    } else if (str_eq(n, "memcmp")) {
        r = (uint32_t)memcmp(g_mem + A(0), g_mem + A(1), A(2));
    } else if (str_eq(n, "memcpy_s")) {
        if (A(1) < A(3)) { r = 34; }
        else { memcpy(g_mem + A(0), g_mem + A(2), A(3)); r = 0; }
    } else if (str_eq(n, "memmove_s")) {
        if (A(1) < A(3)) { r = 34; }
        else { memmove(g_mem + A(0), g_mem + A(2), A(3)); r = 0; }
    } else if (str_eq(n, "strlen")) {
        r = (uint32_t)strlen(gs(A(0)));
    } else if (str_eq(n, "strcpy")) {
        strcpy(((char *)(g_mem + A(0))), gs(A(1))); r = A(0);
    } else if (str_eq(n, "strncpy")) {
        strncpy(((char *)(g_mem + A(0))), gs(A(1)), A(2)); r = A(0);
    } else if (str_eq(n, "strcpy_s")) {
        const char *s = gs(A(2));
        if (A(1) <= strlen(s)) r = 34;
        else { strcpy(((char *)(g_mem + A(0))), s); r = 0; }
    } else if (str_eq(n, "strncpy_s")) {
        const char *s = gs(A(2));
        uint32_t n = A(3);
        if (A(1) <= (n < strlen(s) ? n : strlen(s))) r = 34;
        else { strncpy(((char *)(g_mem + A(0))), s, n); g_mem[A(0) + n] = 0; r = 0; }
    } else if (str_eq(n, "strcat")) {
        strcat(((char *)(g_mem + A(0))), gs(A(1))); r = A(0);
    } else if (str_eq(n, "strncat")) {
        strncat(((char *)(g_mem + A(0))), gs(A(1)), A(2)); r = A(0);
    } else if (str_eq(n, "strcmp")) {
        r = (uint32_t)strcmp(gs(A(0)), gs(A(1)));
    } else if (str_eq(n, "strncmp")) {
        r = (uint32_t)strncmp(gs(A(0)), gs(A(1)), A(2));
    } else if (str_eq(n, "_stricmp")) {
        r = (uint32_t)strcasecmp(gs(A(0)), gs(A(1)));
    } else if (str_eq(n, "_strnicmp")) {
        r = (uint32_t)strncasecmp(gs(A(0)), gs(A(1)), A(2));
    } else if (str_eq(n, "strchr")) {
        const char *p = strchr(gs(A(0)), (int)A(1));
        r = p ? (uint32_t)(p - (const char *)g_mem) : 0;
    } else if (str_eq(n, "strrchr")) {
        const char *p = strrchr(gs(A(0)), (int)A(1));
        r = p ? (uint32_t)(p - (const char *)g_mem) : 0;
    } else if (str_eq(n, "strstr")) {
        const char *p = strstr(gs(A(0)), gs(A(1)));
        r = p ? (uint32_t)(p - (const char *)g_mem) : 0;
    } else if (str_eq(n, "strcspn")) {
        r = (uint32_t)strcspn(gs(A(0)), gs(A(1)));
    } else if (str_eq(n, "strpbrk")) {
        const char *p = strpbrk(gs(A(0)), gs(A(1)));
        r = p ? (uint32_t)(p - (const char *)g_mem) : 0;
    } else if (str_eq(n, "strtok")) {
        static char *save;
        char *p = strtok_r((char *)(g_mem + A(0)), gs(A(1)), &save);
        r = p ? (uint32_t)(p - (const char *)g_mem) : 0;
    } else if (str_eq(n, "strcoll")) {
        r = (uint32_t)strcoll(gs(A(0)), gs(A(1)));
    } else if (str_eq(n, "atoi")) {
        r = (uint32_t)atoi(gs(A(0)));
    } else if (str_eq(n, "atof")) {
        x87_push(c, atof(gs(A(0))));
    } else if (str_eq(n, "strtol")) {
        r = (uint32_t)strtol(gs(A(0)), 0, (int)A(2));
    } else if (str_eq(n, "strtoul")) {
        r = (uint32_t)strtoul(gs(A(0)), 0, (int)A(2));
    } else if (str_eq(n, "_ltoa")) {
        char buf[40]; snprintf(buf, sizeof buf, "%ld", (long)(int32_t)A(0));
        strcpy(((char *)(g_mem + A(1))), buf); r = A(1);
    } else if (str_eq(n, "_itoa_s")) {
        char buf[40]; snprintf(buf, sizeof buf, "%d", (int)A(0));
        if (strlen(buf) + 1 > A(1)) r = 34;
        else { strcpy(((char *)(g_mem + A(2))), buf); r = 0; }
    } else if (str_eq(n, "abs")) {
        r = (uint32_t)(A(0) & 0x80000000u ? -(int32_t)A(0) : (int32_t)A(0));
    } else if (str_eq(n, "tolower")) {
        r = (uint32_t)tolower((int)A(0));
    } else if (str_eq(n, "toupper")) {
        r = (uint32_t)toupper((int)A(0));
    } else if (str_eq(n, "isspace")) {
        r = (uint32_t)(isspace((int)A(0)) != 0);
    } else if (str_eq(n, "isprint")) {
        r = (uint32_t)(isprint((int)A(0)) != 0);
    } else if (str_eq(n, "isdigit")) {
        r = (uint32_t)(isdigit((int)A(0)) != 0);
    } else if (str_eq(n, "rand")) {
        r = (uint32_t)rand();
    } else if (str_eq(n, "srand")) {
        srand(A(0)); r = 0;

    /* ---- printf family ---------------------------------------------- */
    } else if (str_eq(n, "printf")) {
        char buf[4096];
        int len = gfmt(buf, sizeof buf, gs(A(0)), (uint32_t *)(g_mem + c->esp + 8));
        fwrite(buf, 1, (size_t)len, stdout); r = (uint32_t)len;
    } else if (str_eq(n, "fprintf")) {
        char buf[4096];
        FILE *fp = file_of(A(0));
        int len = gfmt(buf, sizeof buf, gs(A(1)), (uint32_t *)(g_mem + c->esp + 12));
        if (fp) fwrite(buf, 1, (size_t)len, fp);
        r = (uint32_t)len;
    } else if (str_eq(n, "sprintf") || str_eq(n, "_snprintf")) {
        int is_sp = str_eq(n, "sprintf");
        uint32_t cap = is_sp ? 0x7FFFFFFFu : A(1);
        uint32_t fmtva = is_sp ? A(1) : A(2);
        uint32_t argoff = is_sp ? 12u : 16u;
        char buf[4096];
        int len = gfmt(buf, cap < sizeof buf ? cap : sizeof buf, gs(fmtva),
                       (uint32_t *)(g_mem + c->esp + argoff));
        size_t ncopy = strlen(buf) + 1;
        if (!is_sp && ncopy > A(1)) ncopy = A(1);
        memcpy(g_mem + A(0), buf, ncopy);
        if (ncopy) g_mem[A(0) + ncopy - 1] = 0;
        r = (uint32_t)len;
    } else if (str_eq(n, "sprintf_s") || str_eq(n, "vsprintf_s")) {
        uint32_t cap = A(1);
        char buf[4096];
        int len;
        if (str_eq(n, "sprintf_s"))
            len = gfmt(buf, cap < sizeof buf ? cap : sizeof buf, gs(A(2)), (uint32_t *)(g_mem + c->esp + 16));
        else
            len = gfmt(buf, cap < sizeof buf ? cap : sizeof buf, gs(A(2)), (uint32_t *)(g_mem + A(3)));
        size_t ncopy = strlen(buf) + 1;
        if (ncopy > cap) ncopy = cap;
        if (ncopy) memcpy(g_mem + A(0), buf, ncopy);
        r = (uint32_t)len;
    } else if (str_eq(n, "vsprintf") || str_eq(n, "_vsnprintf")) {
        int is_vsp = str_eq(n, "vsprintf");
        uint32_t cap = is_vsp ? 0x7FFFFFFFu : A(1);
        uint32_t fmtva = is_vsp ? A(1) : A(2);
        uint32_t va = is_vsp ? A(2) : A(3);
        char buf[4096];
        int len = gfmt(buf, cap < sizeof buf ? cap : sizeof buf, gs(fmtva), (uint32_t *)(g_mem + va));
        size_t ncopy = strlen(buf) + 1;
        if (!is_vsp && ncopy > A(1)) ncopy = A(1);
        memcpy(g_mem + A(0), buf, ncopy);
        if (ncopy) g_mem[A(0) + ncopy - 1] = 0;
        r = (uint32_t)len;
    } else if (str_eq(n, "_vscprintf")) {
        char buf[4096];
        r = (uint32_t)gfmt(buf, sizeof buf, gs(A(0)), (uint32_t *)(g_mem + A(1)));
    } else if (str_eq(n, "sscanf")) {
        r = 0;   /* not needed to boot; the guest handles its own parsing */
    } else if (str_eq(n, "fflush")) {
        FILE *fp = file_of(A(0));
        r = fp ? (uint32_t)fflush(fp) : 0;

    /* ---- file I/O ---------------------------------------------------- */
    } else if (str_eq(n, "fopen")) {
        FILE *fp = fopen(xp(gs(A(0))), gs(A(1)));
        r = fp ? file_new(fp) : 0;
    } else if (str_eq(n, "fclose")) {
        FILE *fp = file_of(A(0));
        if (fp && fp != stdin && fp != stdout && fp != stderr) fclose(fp);
        if ((A(0) & 0xF0000000u) == FH_BASE) g_files[(A(0) >> 4) & 0x3F].used = 0;
        r = 0;
    } else if (str_eq(n, "fread")) {
        FILE *fp = file_of(A(3));
        r = fp ? (uint32_t)fread(g_mem + A(0), A(1), A(2), fp) : 0;
    } else if (str_eq(n, "fwrite")) {
        FILE *fp = file_of(A(3));
        r = fp ? (uint32_t)fwrite(g_mem + A(0), A(1), A(2), fp) : 0;
    } else if (str_eq(n, "fseek")) {
        FILE *fp = file_of(A(0));
        r = fp ? (uint32_t)fseek(fp, (long)(int32_t)A(1), (int)A(2)) : 0;
    } else if (str_eq(n, "ftell")) {
        FILE *fp = file_of(A(0));
        r = fp ? (uint32_t)ftell(fp) : 0;
    } else if (str_eq(n, "fgets")) {
        FILE *fp = file_of(A(2));
        char *p = fp ? fgets((char *)(g_mem + A(0)), (int)A(1), fp) : 0;
        r = p ? A(0) : 0;
    } else if (str_eq(n, "fputs")) {
        FILE *fp = file_of(A(1));
        r = fp ? (uint32_t)fputs(gs(A(0)), fp) : 0;
    } else if (str_eq(n, "remove")) {
        r = (uint32_t)remove(xp(gs(A(0))));
    } else if (str_eq(n, "rename")) {
        r = (uint32_t)rename(xp(gs(A(0))), xp(gs(A(1))));
    } else if (str_eq(n, "_open")) {
        uint32_t f = A(1);
        int flags = (f & 3u) == 0 ? O_RDONLY : (f & 3u) == 1 ? O_WRONLY : O_RDWR;
        if (f & 0x0008u) flags |= O_APPEND;
        if (f & 0x0100u) flags |= O_CREAT;
        if (f & 0x0200u) flags |= O_TRUNC;
        if (f & 0x0400u) flags |= O_EXCL;
        r = (uint32_t)open(xp(gs(A(0))), flags | O_BINARY, (mode_t)A(2));
    } else if (str_eq(n, "_sopen")) {
        uint32_t f = A(1);
        int flags = (f & 3u) == 0 ? O_RDONLY : (f & 3u) == 1 ? O_WRONLY : O_RDWR;
        if (f & 0x0100u) flags |= O_CREAT;
        if (f & 0x0200u) flags |= O_TRUNC;
        r = (uint32_t)open(xp(gs(A(0))), flags | O_BINARY, (mode_t)A(3));
    } else if (str_eq(n, "_close")) {
        r = (uint32_t)close((int)A(0));
    } else if (str_eq(n, "_read")) {
        r = (uint32_t)read((int)A(0), g_mem + A(1), A(2));
    } else if (str_eq(n, "_write")) {
        r = (uint32_t)write((int)A(0), g_mem + A(1), A(2));
    } else if (str_eq(n, "_lseek")) {
        r = (uint32_t)lseek((int)A(0), (off_t)(int32_t)A(1), (int)A(2));
    } else if (str_eq(n, "_filelength")) {
        off_t cur = lseek((int)A(0), 0, SEEK_CUR);
        off_t end = lseek((int)A(0), 0, SEEK_END);
        lseek((int)A(0), cur, SEEK_SET);
        r = (uint32_t)end;
    } else if (str_eq(n, "_chmod")) {
        r = (uint32_t)chmod(xp(gs(A(0))), (mode_t)A(1));
    } else if (str_eq(n, "_mkdir")) {
        r = (uint32_t)mkdir(xp(gs(A(0))), 0755);
    } else if (str_eq(n, "_rmdir")) {
        r = (uint32_t)rmdir(xp(gs(A(0))));
    } else if (str_eq(n, "_getcwd")) {
        char *p = getcwd((char *)(g_mem + A(0)), (size_t)A(1));
        r = p ? A(0) : 0;
    } else if (str_eq(n, "_fullpath")) {
        const char *p = xp(gs(A(0)));
        char *rp = realpath(p, 0);
        snprintf((char *)(g_mem + A(1)), (size_t)A(2), "%s", rp ? rp : p);
        free(rp);
        r = A(1);
    } else if (str_eq(n, "_getdrive")) {
        r = 3;   /* C: */
    } else if (str_eq(n, "_getdiskfree")) {
        if (A(1)) memset(g_mem + A(1), 0, 0x20);
        r = 0;
    } else if (str_eq(n, "__iob_func")) {
        r = IOB_BASE;
    } else if (str_eq(n, "CreateDirectoryA")) {
        r = (uint32_t)mkdir(xp(gs(A(0))), 0755);
    } else if (str_eq(n, "DeleteFileA") || str_eq(n, "RemoveDirectoryA")) {
        r = (uint32_t)(str_eq(n, "DeleteFileA") ? remove(xp(gs(A(0)))) : rmdir(xp(gs(A(0)))));
    } else if (str_eq(n, "MoveFileA")) {
        r = (uint32_t)rename(xp(gs(A(0))), xp(gs(A(1))));
    } else if (str_eq(n, "CreateFileA")) {
        uint32_t acc = A(1), disp = A(4);
        int flags = O_RDONLY;
        if ((acc & 0xC0000000u) == 0xC0000000u) flags = O_RDWR;
        else if (acc & 0x40000000u) flags = O_WRONLY;
        else if (acc & 0x80000000u) flags = O_RDONLY;
        if (disp == 1) flags |= O_CREAT | O_EXCL;
        else if (disp == 2) flags |= O_CREAT | O_TRUNC;
        else if (disp == 4) flags |= O_CREAT;
        else if (disp == 5) flags |= O_TRUNC;
        int fd = open(xp(gs(A(0))), flags | O_BINARY, 0644);
        r = fd >= 0 ? 0x00020000u + (uint32_t)fd : 0xFFFFFFFFu;
    } else if (str_eq(n, "ReadFile")) {
        r = (uint32_t)read((int)(A(0) - 0x20000u), g_mem + A(1), A(2));
        if (A(3)) MR32(A(3)) = r;
    } else if (str_eq(n, "WriteFile")) {
        r = (uint32_t)write((int)(A(0) - 0x20000u), g_mem + A(1), A(2));
        if (A(3)) MR32(A(3)) = r;
    } else if (str_eq(n, "SetFilePointer")) {
        r = (uint32_t)lseek((int)(A(0) - 0x20000u), (off_t)(int32_t)A(1), (int)A(3));
    } else if (str_eq(n, "GetFileSize")) {
        off_t cur = lseek((int)(A(0) - 0x20000u), 0, SEEK_CUR);
        off_t end = lseek((int)(A(0) - 0x20000u), 0, SEEK_END);
        lseek((int)(A(0) - 0x20000u), cur, SEEK_SET);
        if (A(1)) MR32(A(1)) = 0;
        r = (uint32_t)end;
    } else if (str_eq(n, "CloseHandle")) {
        HEnt *h = h_get(A(0));
        if (h && h->kind == HK_THREAD) h->used = 0;
        else if (A(0) - 0x20000u < 0x10000u) close((int)(A(0) - 0x20000u));
        r = 1;

    /* ---- time -------------------------------------------------------- */
    } else if (str_eq(n, "GetTickCount") || str_eq(n, "timeGetTime")) {
        r = (uint32_t)real_ms();
    } else if (str_eq(n, "GetSystemTimeAsFileTime")) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        uint64_t ft = ((uint64_t)ts.tv_sec + 11644473600ull) * 10000000ull +
                      (uint64_t)ts.tv_nsec / 100u;
        if (A(0)) { MR32(A(0)) = (uint32_t)ft; MR32(A(0) + 4) = (uint32_t)(ft >> 32); }
        r = 0;
    } else if (str_eq(n, "GetSystemTime")) {
        struct tm tm;
        time_t t = time(0);
        gmtime_r(&t, &tm);
        if (A(0)) {
            MR16(A(0) + 0) = (uint16_t)(tm.tm_year + 1900);
            MR16(A(0) + 2) = (uint16_t)(tm.tm_mon + 1);
            MR16(A(0) + 6) = (uint16_t)tm.tm_wday;
            MR16(A(0) + 8) = (uint16_t)tm.tm_mday;
            MR16(A(0) + 10) = (uint16_t)tm.tm_hour;
            MR16(A(0) + 12) = (uint16_t)tm.tm_min;
            MR16(A(0) + 14) = (uint16_t)tm.tm_sec;
        }
        r = 0;
    } else if (str_eq(n, "SystemTimeToFileTime")) {
        r = 1;
    } else if (str_eq(n, "QueryPerformanceCounter")) {
        uint64_t v = real_us();
        if (A(0)) { MR32(A(0)) = (uint32_t)v; MR32(A(0) + 4) = (uint32_t)(v >> 32); }
        r = 1;
    } else if (str_eq(n, "QueryPerformanceFrequency")) {
        if (A(0)) { MR32(A(0)) = 1000000u; MR32(A(0) + 4) = 0; }
        r = 1;
    } else if (str_eq(n, "Sleep") || str_eq(n, "SleepEx")) {
        uint32_t ms = A(0);
        if (ms > 0 && ms < 100000u) {
            struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
            nanosleep(&ts, 0);
        }
        r = 0;
    } else if (str_eq(n, "timeBeginPeriod") || str_eq(n, "timeEndPeriod")) {
        r = 0;
    } else if (str_eq(n, "clock")) {
        r = (uint32_t)((uint64_t)clock() * 1000ull / CLOCKS_PER_SEC);
    } else if (str_eq(n, "time") || str_eq(n, "_time64")) {
        int64_t t = (int64_t)time(0);
        if (A(0)) { MR32(A(0)) = (uint32_t)t; MR32(A(0) + 4) = (uint32_t)(t >> 32); }
        c->eax = (uint32_t)t; c->edx = (uint32_t)(t >> 32);
    } else if (str_eq(n, "_ftime64")) {
        struct timeval tv;
        gettimeofday(&tv, 0);
        if (A(0)) {
            MR32(A(0)) = (uint32_t)tv.tv_sec; MR32(A(0) + 4) = 0;
            MR16(A(0) + 8) = (uint16_t)(tv.tv_usec / 1000);
            MR16(A(0) + 10) = 0; MR16(A(0) + 12) = 0;
        }
        r = 0;
    } else if (str_eq(n, "_localtime64") || str_eq(n, "_localtime32") || str_eq(n, "_gmtime64")) {
        uint32_t p = A(0);
        int64_t v = (int64_t)((uint64_t)MR32(p) | ((uint64_t)MR32(p + 4) << 32));
        time_t t = (time_t)v;
        struct tm tm;
        if (str_eq(n, "_gmtime64")) gmtime_r(&t, &tm); else localtime_r(&t, &tm);
        uint32_t out = 0x1B021000u;
        mem_commit(out, 0x40);
        MR32(out + 0) = (uint32_t)tm.tm_sec;
        MR32(out + 4) = (uint32_t)tm.tm_min;
        MR32(out + 8) = (uint32_t)tm.tm_hour;
        MR32(out + 12) = (uint32_t)tm.tm_mday;
        MR32(out + 16) = (uint32_t)tm.tm_mon;
        MR32(out + 20) = (uint32_t)tm.tm_year;
        MR32(out + 24) = (uint32_t)tm.tm_wday;
        MR32(out + 28) = (uint32_t)tm.tm_yday;
        MR32(out + 32) = (uint32_t)tm.tm_isdst;
        r = out;
    } else if (str_eq(n, "_mktime64") || str_eq(n, "_mkgmtime64")) {
        uint32_t p = A(0);
        struct tm tm;
        memset(&tm, 0, sizeof tm);
        tm.tm_sec = (int)MR32(p); tm.tm_min = (int)MR32(p + 4);
        tm.tm_hour = (int)MR32(p + 8); tm.tm_mday = (int)MR32(p + 12);
        tm.tm_mon = (int)MR32(p + 16); tm.tm_year = (int)MR32(p + 20);
        time_t t = str_eq(n, "_mktime64") ? mktime(&tm) : timegm(&tm);
        c->eax = (uint32_t)t; c->edx = (uint32_t)((int64_t)t >> 32);
    } else if (str_eq(n, "_difftime64")) {
        int64_t a = (int64_t)((uint64_t)A(0) | ((uint64_t)A(1) << 32));
        int64_t b = (int64_t)((uint64_t)A(2) | ((uint64_t)A(3) << 32));
        x87_push(c, (double)(a - b));
    } else if (str_eq(n, "asctime")) {
        uint32_t p = A(0);
        struct tm tm;
        memset(&tm, 0, sizeof tm);
        tm.tm_sec = (int)MR32(p); tm.tm_min = (int)MR32(p + 4);
        tm.tm_hour = (int)MR32(p + 8); tm.tm_mday = (int)MR32(p + 12);
        tm.tm_mon = (int)MR32(p + 16); tm.tm_year = (int)MR32(p + 20);
        tm.tm_wday = (int)MR32(p + 24); tm.tm_yday = (int)MR32(p + 28);
        char *s = asctime(&tm);
        uint32_t out = 0x1B021800u;
        mem_commit(out, 0x40);
        snprintf((char *)(g_mem + out), 0x40, "%s", s ? s : "");
        r = out;

    /* ---- math -------------------------------------------------------- */
    } else if (str_eq(n, "ceil")) { x87_push(c, ceil(*(double *)(g_mem + A(0))));
    } else if (str_eq(n, "floor")) { x87_push(c, floor(*(double *)(g_mem + A(0))));
    } else if (str_eq(n, "sqrt")) { x87_push(c, sqrt(*(double *)(g_mem + A(0))));
    } else if (str_eq(n, "cos")) { x87_push(c, cos(*(double *)(g_mem + A(0))));
    } else if (str_eq(n, "ldexp")) { x87_push(c, ldexp(*(double *)(g_mem + A(0)), (int)A(2)));
    } else if (str_eq(n, "_CIsqrt")) { x87_push(c, sqrt(*(double *)(g_mem + A(0))));
    } else if (str_eq(n, "_CIfmod")) { x87_push(c, fmod(*(double *)(g_mem + A(0)), *(double *)(g_mem + A(2))));
    } else if (str_eq(n, "_CIpow")) { x87_push(c, pow(*(double *)(g_mem + A(0)), *(double *)(g_mem + A(2))));
    } else if (str_eq(n, "_CIsin")) { x87_push(c, sin(*(double *)(g_mem + A(0))));
    } else if (str_eq(n, "_CIcos")) { x87_push(c, cos(*(double *)(g_mem + A(0))));
    } else if (str_eq(n, "_CIatan2")) { x87_push(c, atan2(*(double *)(g_mem + A(0)), *(double *)(g_mem + A(2))));
    } else if (str_eq(n, "_CIacos")) { x87_push(c, acos(*(double *)(g_mem + A(0))));
    } else if (str_eq(n, "_CIasin")) { x87_push(c, asin(*(double *)(g_mem + A(0))));
    } else if (str_eq(n, "_CIlog")) { x87_push(c, log(*(double *)(g_mem + A(0))));
    } else if (str_eq(n, "_CIlog10")) { x87_push(c, log10(*(double *)(g_mem + A(0))));
    } else if (str_eq(n, "_CIexp")) { x87_push(c, exp(*(double *)(g_mem + A(0))));
    } else if (str_eq(n, "_CItan")) { x87_push(c, tan(*(double *)(g_mem + A(0))));
    } else if (str_eq(n, "_CItanh")) { x87_push(c, tanh(*(double *)(g_mem + A(0))));
    } else if (str_eq(n, "_CIsinh")) { x87_push(c, sinh(*(double *)(g_mem + A(0))));
    } else if (str_eq(n, "_CIcosh")) { x87_push(c, cosh(*(double *)(g_mem + A(0))));
    } else if (str_eq(n, "_CIatan")) { x87_push(c, atan(*(double *)(g_mem + A(0))));
    } else if (str_eq(n, "_isnan")) {
        r = (uint32_t)(isnan(*(double *)(g_mem + A(0))) != 0);

    /* ---- process / system -------------------------------------------- */
    } else if (str_eq(n, "GetCurrentProcessId")) {
        r = (uint32_t)getpid();
    } else if (str_eq(n, "GetCurrentThreadId")) {
        r = (uint32_t)(uintptr_t)pthread_self();
    } else if (str_eq(n, "GetCurrentProcess")) {
        r = 0xFFFFFFFFu;
    } else if (str_eq(n, "GetCurrentThread")) {
        r = 0xFFFFFFFEu;
    } else if (str_eq(n, "GetCommandLineA")) {
        r = g_cmdline_va;
    } else if (str_eq(n, "GetModuleHandleA")) {
        r = 0x00400000u;
    } else if (str_eq(n, "GetModuleFileNameA")) {
        const char *s = "nfsps.exe";
        uint32_t n2 = A(2);
        snprintf((char *)(g_mem + A(1)), n2, "%s", s);
        r = (uint32_t)strlen(s);
    } else if (str_eq(n, "GetVersionExA")) {
        uint32_t p = A(0);
        if (p) {
            /* OSVERSIONINFOA: do not touch dwOSVersionInfoSize at +0 */
            MR32(p + 4) = 5; MR32(p + 8) = 1; MR32(p + 12) = 2600;
            MR32(p + 16) = 2;              /* VER_PLATFORM_WIN32_NT */
            g_mem[p + 20] = 0;             /* szCSDVersion */
        }
        r = 1;
    } else if (str_eq(n, "GetSystemInfo")) {
        uint32_t p = A(0);
        if (p) {
            memset(g_mem + p, 0, 0x24);
            MR32(p + 0) = 0;          /* PROCESSOR_ARCHITECTURE_INTEL */
            MR32(p + 4) = 0x1000;     /* page size */
            MR32(p + 8) = 0x10000;    /* minimum application address */
            MR32(p + 12) = 0x7FFEFFFFu; /* maximum application address */
            MR32(p + 16) = 1;         /* active processor mask */
            MR32(p + 20) = 1;         /* processors */
            MR32(p + 24) = 586;       /* processor type */
            MR32(p + 28) = 0x10000;   /* allocation granularity */
        }
        r = 0;
    } else if (str_eq(n, "GetCurrentDirectoryA")) {
        snprintf((char *)(g_mem + A(1)), A(0), "%s", g_gamedir);
        r = (uint32_t)strlen(g_gamedir);
    } else if (str_eq(n, "GetSystemDirectoryA")) {
        snprintf((char *)(g_mem + A(0)), A(1), "C:\\WINDOWS\\SYSTEM32");
        r = 19;
    } else if (str_eq(n, "GetLongPathNameA")) {
        snprintf((char *)(g_mem + A(1)), A(2), "%s", gs(A(0)));
        r = (uint32_t)strlen(gs(A(0)));
    } else if (str_eq(n, "GetDriveTypeA")) {
        r = 3;
    } else if (str_eq(n, "GetLogicalDrives")) {
        r = 0x0F;
    } else if (str_eq(n, "GetDiskFreeSpaceExA")) {
        if (A(1)) { MR32(A(1)) = 0x40000000u; MR32(A(1) + 4) = 0; }
        if (A(2)) { MR32(A(2)) = 0x20000000u; MR32(A(2) + 4) = 0; }
        if (A(3)) { MR32(A(3)) = 0x20000000u; MR32(A(3) + 4) = 0; }
        r = 1;
    } else if (str_eq(n, "SetErrorMode")) {
        r = 0;
    } else if (str_eq(n, "IsDebuggerPresent")) {
        r = 0;
    } else if (str_eq(n, "GetLastError")) {
        r = 0;
    } else if (str_eq(n, "OutputDebugStringA")) {
        fprintf(stderr, "[guest] %s\n", gs(A(0)));
        r = 0;
    } else if (str_eq(n, "IsBadReadPtr")) {
        uint32_t p = A(0), n2 = A(1);
        r = (uint32_t)(!p || !mem_is_committed(p) || (n2 && !mem_is_committed(p + n2 - 1)));
    } else if (str_eq(n, "RaiseException")) {
        uint32_t code = A(0), eflags = A(1), nargs = A(2), aptr = A(3);
        if (!seh_dispatch(c, code, eflags, nargs, aptr)) {
            fprintf(stderr, "\n*** unhandled guest exception %08x raised at %08x\n",
                    code, c->eip);
            guest_backtrace(c, 12);
            guest_exit(255);
        }
        r = 0;
    } else if (str_eq(n, "_except_handler3")) {
        /* MSVCR80's x86 establisher.  The Windows dispatcher calls it with
         * (ExceptionRecord, EstablisherFrame, ContextRecord,
         * DispatcherContext) and it answers with an EXCEPTION_DISPOSITION.
         * It is __cdecl, so the arguments are still on the stack here. */
        uint32_t base = c->esp;
        uint32_t disp = seh_msvc3(c, MR32(base), MR32(base + 4),
                                  MR32(base + 8), MR32(base + 12));
        c->esp = base;
        r = disp;
    } else if (str_eq(n, "SetUnhandledExceptionFilter")) {
        r = 0;
    } else if (str_eq(n, "UnhandledExceptionFilter")) {
        r = 1;
    } else if (str_eq(n, "TerminateThread")) {
        r = 1;
    } else if (str_eq(n, "SuspendThread") || str_eq(n, "ResumeThread") ||
               str_eq(n, "SetThreadPriority") || str_eq(n, "GetThreadPriority") ||
               str_eq(n, "SetThreadIdealProcessor") || str_eq(n, "SetPriorityClass") ||
               str_eq(n, "GetPriorityClass")) {
        r = 0;
    } else if (str_eq(n, "GetExitCodeThread")) {
        HEnt *h = h_get(A(0));
        if (A(1)) MR32(A(1)) = h && h->done ? h->code : 259;
        r = 1;
    } else if (str_eq(n, "CreateThread")) {
        r = thread_create(A(2), A(3), 0);
    } else if (str_eq(n, "_beginthreadex")) {
        r = thread_create(A(2), A(3), 1);
        /* the CRT wants the thread id in *thrdaddr (argument 6) */
        uint32_t tid_ptr = A(5);
        if (tid_ptr && mem_is_committed(tid_ptr)) {
            HEnt *e = h_get(r);
            MR32(tid_ptr) = e ? MR32(e->teb + 0x24) : 0;
        }
    } else if (str_eq(n, "_endthreadex")) {
        pthread_exit(0);
    } else if (str_eq(n, "WaitForSingleObject")) {
        HEnt *h = h_get(A(0));
        uint32_t ms = A(1);
        if (!h) { r = 0xFFFFFFFFu; }
        else if (h->kind == HK_THREAD) {
            pthread_mutex_lock(&h->m);
            if (ms == 0xFFFFFFFFu) { while (!h->done) pthread_cond_wait(&h->cv, &h->m); }
            else {
                struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
                ts.tv_sec += ms / 1000; ts.tv_nsec += (long)(ms % 1000) * 1000000L;
                if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
                while (!h->done) { if (pthread_cond_timedwait(&h->cv, &h->m, &ts) == ETIMEDOUT) break; }
            }
            r = h->done ? 0 : 0x102u;
            pthread_mutex_unlock(&h->m);
        } else if (h->kind == HK_EVENT) {
            pthread_mutex_lock(&h->m);
            if (ms == 0xFFFFFFFFu) { while (!h->signaled) pthread_cond_wait(&h->cv, &h->m); }
            else {
                struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
                ts.tv_sec += ms / 1000; ts.tv_nsec += (long)(ms % 1000) * 1000000L;
                if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
                while (!h->signaled) { if (pthread_cond_timedwait(&h->cv, &h->m, &ts) == ETIMEDOUT) break; }
            }
            r = h->signaled ? 0 : 0x102u;
            if (h->signaled && !h->manual) h->signaled = 0;
            pthread_mutex_unlock(&h->m);
        } else if (h->kind == HK_SEM) {
            pthread_mutex_lock(&h->m);
            while (h->count <= 0) pthread_cond_wait(&h->cv, &h->m);
            h->count--;
            pthread_mutex_unlock(&h->m);
            r = 0;
        } else {
            r = 0;
        }
    } else if (str_eq(n, "CreateEventA")) {
        r = h_make(HK_EVENT);
        HEnt *h = h_get(r);
        if (h) { h->manual = (A(1) != 0); h->signaled = (A(2) != 0); }
    } else if (str_eq(n, "SetEvent") || str_eq(n, "PulseEvent")) {
        HEnt *h = h_get(A(0));
        if (h) { pthread_mutex_lock(&h->m); h->signaled = 1; pthread_cond_broadcast(&h->cv); pthread_mutex_unlock(&h->m); }
        r = 1;
    } else if (str_eq(n, "ResetEvent")) {
        HEnt *h = h_get(A(0));
        if (h) { pthread_mutex_lock(&h->m); h->signaled = 0; pthread_mutex_unlock(&h->m); }
        r = 1;
    } else if (str_eq(n, "CreateMutexA") || str_eq(n, "CreateMutexW")) {
        r = h_make(HK_MUTEX);
    } else if (str_eq(n, "ReleaseMutex")) {
        r = 1;
    } else if (str_eq(n, "CreateSemaphoreA")) {
        r = h_make(HK_SEM);
        HEnt *h = h_get(r);
        if (h) h->count = (long)A(2);
    } else if (str_eq(n, "ReleaseSemaphore")) {
        HEnt *h = h_get(A(0));
        if (h) { pthread_mutex_lock(&h->m); h->count += (long)A(1); pthread_cond_broadcast(&h->cv); pthread_mutex_unlock(&h->m); }
        r = 1;
    } else if (str_eq(n, "DuplicateHandle")) {
        r = 0;
    } else if (str_eq(n, "InitializeCriticalSection") || str_eq(n, "DeleteCriticalSection") ||
               str_eq(n, "EnterCriticalSection") || str_eq(n, "LeaveCriticalSection")) {
        r = 0;
    } else if (str_eq(n, "TryEnterCriticalSection")) {
        r = 1;
    } else if (str_eq(n, "CreateWaitableTimerA")) {
        r = h_make(HK_EVENT);
    } else if (str_eq(n, "SetWaitableTimer")) {
        r = 1;
    } else if (str_eq(n, "CreateToolhelp32Snapshot")) {
        r = 0xFFFFFFFFu;
    } else if (str_eq(n, "Process32First") || str_eq(n, "Process32Next")) {
        r = 0;
    } else if (str_eq(n, "InterlockedCompareExchange")) {
        uint32_t *p = (uint32_t *)(g_mem + A(0));
        uint32_t cmp = A(2), xchg = A(1);
        __atomic_compare_exchange_n(p, &cmp, xchg, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
        r = cmp;   /* previous value, whether or not the exchange happened */
    } else if (str_eq(n, "InterlockedExchange")) {
        r = __atomic_exchange_n((uint32_t *)(g_mem + A(0)), A(1), __ATOMIC_SEQ_CST);
    } else if (str_eq(n, "InterlockedExchangeAdd")) {
        r = __atomic_fetch_add((uint32_t *)(g_mem + A(0)), A(1), __ATOMIC_SEQ_CST);
    } else if (str_eq(n, "InterlockedDecrement")) {
        r = __atomic_sub_fetch((uint32_t *)(g_mem + A(0)), 1u, __ATOMIC_SEQ_CST);
    } else if (str_eq(n, "InterlockedIncrement")) {
        r = __atomic_add_fetch((uint32_t *)(g_mem + A(0)), 1u, __ATOMIC_SEQ_CST);
    } else if (str_eq(n, "TlsAlloc")) {
        r = g_tls_next_index++;
    } else if (str_eq(n, "TlsGetValue")) {
        uint32_t s = tls_slot(c, A(0));
        r = s ? MR32(s) : 0;
    } else if (str_eq(n, "TlsSetValue")) {
        uint32_t s = tls_slot(c, A(0));
        if (s) MR32(s) = A(1);
        r = 1;

    /* ---- encoding ---------------------------------------------------- */
    } else if (str_eq(n, "MultiByteToWideChar")) {
        const char *s = gs(A(2));
        int len = (int)A(3);
        if (len < 0) len = (int)strlen(s) + 1;
        wchar_t *d = (wchar_t *)(g_mem + A(4));
        for (int i = 0; i < len; i++) d[i] = (wchar_t)(unsigned char)s[i];
        r = (uint32_t)len;
    } else if (str_eq(n, "WideCharToMultiByte")) {
        const wchar_t *s = (const wchar_t *)(g_mem + A(2));
        int len = (int)A(3);
        if (len < 0) len = (int)wcslen(s) + 1;
        char *d = (char *)(g_mem + A(4));
        for (int i = 0; i < len; i++) d[i] = (char)s[i];
        r = (uint32_t)len;

    /* ---- qsort / bsearch --------------------------------------------- */
    } else if (str_eq(n, "qsort")) {
        uint32_t base = A(0), cnt = A(1), sz = A(2), cmp = A(3);
        /* simple insertion sort, stable enough for boot.  The comparator is
         * guest code, so the scratch element must be guest memory too. */
        uint32_t tmp = gh_alloc(sz ? sz : 1, 0, 16);
        for (uint32_t i = 1; i < cnt && tmp; i++) {
            memcpy(g_mem + tmp, g_mem + base + i * sz, sz);
            uint32_t j = i;
            while (j > 0) {
                PUSH32(base + (j - 1) * sz);
                PUSH32(tmp);
                PUSH32(SENTINEL);
                dispatch_indirect(c, cmp);
                while (c->eip != SENTINEL) { uint32_t t = c->eip; c->eip = 0; dispatch_indirect(c, t); }
                c->esp += 8;
                if ((int32_t)c->eax <= 0) break;
                memcpy(g_mem + base + j * sz, g_mem + base + (j - 1) * sz, sz);
                j--;
            }
            memcpy(g_mem + base + j * sz, g_mem + tmp, sz);
        }
        gh_free(tmp);
        r = 0;
    } else if (str_eq(n, "bsearch")) {
        uint32_t key = A(0), base = A(1), cnt = A(2), sz = A(3), cmp = A(4);
        uint32_t lo = 0, hi = cnt;
        r = 0;
        while (lo < hi) {
            uint32_t mid = (lo + hi) / 2;
            PUSH32(base + mid * sz);
            PUSH32(key);
            PUSH32(SENTINEL);
            dispatch_indirect(c, cmp);
            while (c->eip != SENTINEL) { uint32_t t = c->eip; c->eip = 0; dispatch_indirect(c, t); }
            c->esp += 8;
            int32_t v = (int32_t)c->eax;
            if (v == 0) { r = base + mid * sz; break; }
            if (v < 0) lo = mid + 1; else hi = mid;
        }

    /* ---- getenv / filesystem find ------------------------------------ */
    } else if (str_eq(n, "getenv")) {
        r = 0;
    } else if (str_eq(n, "_findfirst64i32") || str_eq(n, "_findnext64i32")) {
        r = 0xFFFFFFFFu;
    } else if (str_eq(n, "_findclose")) {
        r = 0;

    /* ---- MSVC C++ runtime -------------------------------------------- */
    } else if (str_eq(n, "__CxxThrowException")) {
        fprintf(stderr, "\n*** C++ exception thrown (obj=%08x)\n", A(0));
        guest_exit(255);
    } else if (str_eq(n, "__RTDynamicCast")) {
        r = 0;
    } else if (str_eq(n, "?_type_info_dtor_internal_method@type_info@@QAEXXZ")) {
        r = 0;
    } else if (str_eq(n, "??0exception@std@@QAE@XZ") ||
               str_eq(n, "??0exception@std@@QAE@ABV01@@Z") ||
               str_eq(n, "??1exception@std@@UAE@XZ")) {
        r = c->ecx;
    } else if (strncmp(n, "??0?$basic_string@DU", 20) == 0) {
        uint32_t self = c->ecx;
        if (strstr(n, "QAE@PBD@Z")) {          /* from const char* */
            const char *s = gs(A(0));
            size_t len = strlen(s);
            if (len < 16) {
                memcpy(g_mem + self, s, len + 1);
                MR32(self + 16) = (uint32_t)len;
                MR32(self + 20) = 15;
            } else {
                uint32_t p = gh_alloc((uint32_t)len + 1, 0, 16);
                memcpy(g_mem + p, s, len + 1);
                MR32(self) = p;
                MR32(self + 16) = (uint32_t)len;
                MR32(self + 20) = (uint32_t)len;
            }
        } else {                                /* copy */
            uint32_t src = A(0);
            uint32_t res = MR32(src + 20), len = MR32(src + 16);
            if (res < 16) {
                memcpy(g_mem + self, g_mem + src, len + 1);
                MR32(self + 16) = len;
                MR32(self + 20) = 15;
            } else {
                uint32_t p = gh_alloc(len + 1, 0, 16);
                memcpy(g_mem + p, g_mem + MR32(src), len + 1);
                MR32(self) = p;
                MR32(self + 16) = len;
                MR32(self + 20) = len;
            }
        }
        r = self;
    } else if (strncmp(n, "??1?$basic_string@DU", 20) == 0) {
        uint32_t self = c->ecx;
        if (MR32(self + 20) >= 16) gh_free(MR32(self));
        r = 0;

    /* ---- misc / graphics: explicit stubs ----------------------------- */
    } else if (str_eq(n, "MessageBoxA")) {
        fprintf(stderr, "\n[MessageBoxA] %s\n[MessageBoxA] %s\n", gs(A(1)), gs(A(2)));
        r = 1;
    } else if (str_eq(n, "PeekMessageA") || str_eq(n, "GetMessageA")) {
        r = 0;
    } else if (str_eq(n, "RegisterClassExA")) {
        r = 1;
    } else if (str_eq(n, "CreateWindowExA")) {
        r = 0x00010001u;
    } else if (str_eq(n, "DefWindowProcA")) {
        r = 0;
    } else if (str_eq(n, "GetClientRect") || str_eq(n, "GetWindowRect")) {
        if (A(1)) { MR32(A(1)) = 0; MR32(A(1) + 4) = 0; MR32(A(1) + 8) = 640; MR32(A(1) + 12) = 480; }
        r = 1;
    } else if (str_eq(n, "PostQuitMessage")) {
        r = 0;
    } else if (str_eq(n, "ShowCursor")) {
        r = 0;
    } else if (str_eq(n, "GetForegroundWindow") || str_eq(n, "GetDesktopWindow")) {
        r = 0x00010001u;
    } else if (str_eq(n, "Direct3DCreate9")) {
        r = 0;   /* not reached before the first milestone */
    } else if (str_eq(n, "DirectInput8Create")) {
        /* DirectInput8Create HLE: create IDirectInput8 interface stub */
        uint32_t hInst = A(0);
        uint32_t riidObj = A(1);
        uint32_t ppvOut = A(2);
        static uint32_t di_initialized = 0;
        if (!di_initialized) {
            di_initialized = 1;
            MR32(ppvOut) = 0x10000000;
            r = 1;
        } else {
            r = 0;
        }
        /* Fall through to finish label */
    } else if (strncmp(n, "D3DX", 4) == 0) {
        r = 0;   /* not reached before the first milestone */
    } else {
        handled = 0;
    }

    if (!handled) {
        g_unimpl[idx]++;
        if (g_unimpl_shown < 40) {
            g_unimpl_shown++;
            fprintf(stderr, "  [unimplemented] %s!%s  ret=%08x a0=%08x a1=%08x\n",
                    e->dll, n, c->eip, A(0), A(1));
        }
        r = 0;
    }

    c->eax = r;
    c->esp += 4u * (uint32_t)e->nargs;
    return 1;
}

/* ------------------------------------------------------------------ */
/* init / report                                                        */

/* Call an import from a synthetic guest stack.  win32_call pops the
 * caller's return address itself, so esp starts at the return slot and
 * the arguments follow it. */
static uint32_t imp_call(CPU *c, uint32_t idx, uint32_t stack,
                         const uint32_t *args, int nargs) {
    MR32(stack) = 0xDEADBEEFu;
    for (int i = 0; i < nargs; i++) MR32(stack + 4u + 4u * (uint32_t)i) = args[i];
    c->esp = stack;
    c->eip = 0xDEADBEEFu;
    win32_call(c, idx);
    return c->eax;
}

static uint32_t imp_index(const char *name) {
    for (uint32_t i = 0; i < g_imp_count; i++)
        if (!strcmp(g_imp[i].name, name)) return i;
    return 0xFFFFFFFFu;
}

/* A real worker thread, with its own TEB, writes the same Win32 TlsAlloc slot
 * and the same __declspec(thread) variable as the main thread.  Neither write
 * may be visible to the other, and the two threads must not share the module
 * TLS block. */
struct tls_worker {
    uint32_t teb, slot, stack;
    int win32_ok;
    uint32_t implicit_seen;
};

static void *tls_worker_main(void *arg) {
    struct tls_worker *w = arg;
    CPU c;
    memset(&c, 0, sizeof c);
    c.fs_base = c.gs_base = w->teb;

    /* the image's implicit TLS: fs:[0x2c] -> P, P[0] -> this thread's block */
    uint32_t p = MR32(w->teb + 0x2C);
    uint32_t q = MR32(p);
    MR32(q + 4u) = 0x5555EEEEu;
    w->implicit_seen = MR32(q + 4u);

    uint32_t sv[2] = { w->slot, 0x4444DDDDu };
    uint32_t kv[1] = { w->slot };
    w->win32_ok = imp_call(&c, imp_index("TlsSetValue"), w->stack, sv, 2) != 0 &&
                  imp_call(&c, imp_index("TlsGetValue"), w->stack, kv, 1) == 0x4444DDDDu;
    return 0;
}

/* Two synthetic threads with different TEBs write the same TlsAlloc slot
 * and must read back their own value.  A second slot must not alias the
 * first.  Then a real pthread repeats it.  Runs only when
 * PROSTREET_TLS_TEST is set. */
int win32_tls_selftest(void) {
    uint32_t ia = imp_index("TlsAlloc");
    uint32_t is = imp_index("TlsSetValue");
    uint32_t ig = imp_index("TlsGetValue");
    if (ia == 0xFFFFFFFFu || is == 0xFFFFFFFFu || ig == 0xFFFFFFFFu) {
        fprintf(stderr, "tls-selftest: TlsAlloc/TlsSetValue/TlsGetValue missing\n");
        return 0;
    }

    uint32_t teb_a = TEB_BASE;
    uint32_t teb_b = new_teb(0x70000000u, 0x70100000u);
    uint32_t stack = gh_alloc(0x1000, 1, 16);
    if (!stack) return 0;

    CPU a, b;
    memset(&a, 0, sizeof a);
    memset(&b, 0, sizeof b);
    a.fs_base = a.gs_base = teb_a;
    b.fs_base = b.gs_base = teb_b;

    uint32_t idx = imp_call(&a, ia, stack, 0, 0);
    uint32_t v1 = 0x1111AAAAu, v2 = 0x2222BBBBu;
    uint32_t sa[2] = { idx, v1 }, sb[2] = { idx, v2 };
    imp_call(&a, is, stack, sa, 2);
    imp_call(&b, is, stack, sb, 2);
    uint32_t ga = imp_call(&a, ig, stack, &idx, 1);
    uint32_t gb = imp_call(&b, ig, stack, &idx, 1);

    uint32_t idx2 = imp_call(&a, ia, stack, 0, 0);
    uint32_t sd[2] = { idx2, 0x3333CCCCu };
    imp_call(&a, is, stack, sd, 2);
    uint32_t ga2 = imp_call(&a, ig, stack, &idx2, 1);
    uint32_t gb2 = imp_call(&b, ig, stack, &idx2, 1);

    int ok = ga == v1 && gb == v2 && ga2 == 0x3333CCCCu && gb2 == 0;

    /* A real worker thread: same TlsAlloc slot, same __declspec(thread)
     * variable.  Its TEB (and so its TLS pointer array and module block) is
     * its own, so neither write may show up on the main thread. */
    uint32_t main_p = MR32(TEB_BASE + 0x2C);
    uint32_t main_q = MR32(main_p);
    MR32(main_q + 4u) = 0x11112222u;

    struct tls_worker w;
    memset(&w, 0, sizeof w);
    w.teb = new_teb(0x70200000u, 0x70300000u);
    w.slot = idx;
    w.stack = gh_alloc(0x1000, 1, 16);
    pthread_t th;
    int made = w.stack && pthread_create(&th, 0, tls_worker_main, &w) == 0;
    if (made) pthread_join(th, 0);
    uint32_t wq = MR32(MR32(w.teb + 0x2C));
    uint32_t w_win = MR32(w.teb + TEB_TLS_SLOTS + 4u * idx);
    uint32_t kv[1] = { idx };
    uint32_t m_win = imp_call(&a, ig, stack, kv, 1);
    int iso = made && w.win32_ok && w.implicit_seen == 0x5555EEEEu
              && MR32(wq + 4u) == 0x5555EEEEu
              && MR32(main_q + 4u) == 0x11112222u
              && wq != main_q
              && w_win == 0x4444DDDDu && m_win == v1;

    fprintf(stderr, "tls-selftest: slot%u A=%08x B=%08x slot%u A=%08x B=%08x -> %s\n",
            idx, ga, gb, idx2, ga2, gb2, ok ? "PASS" : "FAIL");
    fprintf(stderr, "tls-selftest: worker slot%u=%08x tls+4=%08x  "
                    "main slot%u=%08x tls+4=%08x  blocks %08x/%08x -> %s\n",
            idx, w_win, MR32(wq + 4u), idx, m_win, MR32(main_q + 4u),
            wq, main_q, iso ? "PASS" : "FAIL");
    return ok && iso;
}

void win32_init(void) {
    g_calls = calloc(g_imp_count, sizeof *g_calls);
    g_unimpl = calloc(g_imp_count, sizeof *g_unimpl);
    g_trace = getenv("PROSTREET_TRACE") != 0;
    { const char *d = getenv("PROSTREET_GAMEDIR"); if (d && *d) g_gamedir = d; }

    mem_commit(TEB_BASE, 0x20000);
    mem_commit(PEB_BASE, 0x1000);
    MR32(PEB_BASE + 0x08) = 0x00400000u;

    mem_commit(CR_FMODE, 0x100);
    MR32(CR_FMODE) = 0;
    MR32(CR_COMMODE) = 0;
    MR32(CR_INITENV) = 0;
    /* _adjust_fdiv: the CRT does  mov eax,[slot]; mov eax,[eax]  */
    MR32(CR_ADJF) = CR_ADJFV;
    MR32(CR_ADJFV) = 0;

    g_cmdline_va = gh_guest_str("nfsps.exe");

    uint32_t initenv_idx = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < g_imp_count; i++) {
        if (g_imp[i].iat == 0x00967398u) { initenv_idx = i; continue; }
        if (g_imp[i].iat == 0x009673acu) { MR32(g_imp[i].iat) = CR_ADJF; continue; }
        MR32(g_imp[i].iat) = IMP_MAGIC + i;
    }
    if (initenv_idx != 0xFFFFFFFFu) MR32(g_imp[initenv_idx].iat) = CR_INITENV;

    fprintf(stderr, "win32: %u imports bound (%s data slots), gamedir \"%s\"\n",
            g_imp_count, initenv_idx != 0xFFFFFFFFu ? "__initenv/_adjust_fdiv" : "no", g_gamedir);
}

void win32_report(void) {
    fprintf(stderr, "\n=== WIN32 LAYER ===\n");
    uint64_t total = 0, unimpl = 0;
    for (uint32_t i = 0; i < g_imp_count; i++) { total += g_calls[i]; unimpl += g_unimpl[i]; }
    fprintf(stderr, "  import calls : %llu\n", (unsigned long long)total);
    fprintf(stderr, "  unimplemented: %llu\n", (unsigned long long)unimpl);
    int shown = 0;
    for (uint32_t i = 0; i < g_imp_count && shown < 30; i++)
        if (g_unimpl[i]) {
            fprintf(stderr, "    %-14s %-40s %u\n", g_imp[i].dll, g_imp[i].name, g_unimpl[i]);
            shown++;
        }
}

void guest_exit(int code) {
    g_guest_exited = 1;
    g_exit_code = code;
    win32_report();
    dispatch_report();
    mem_report();
    fflush(stderr);
    fflush(stdout);
    _exit(code & 0xFF);
}

/* Guest address -> lifted native function.  One table the size of .text:
 * an indirect call or tail jump is a load and a call.  IAT slots hold
 * IMP_MAGIC + index and are routed to win32_call(). */
#include "rt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef void (*guest_fn)(CPU *);
extern const uint32_t g_fn_addr[];
extern const guest_fn g_fn_ptr[];
extern const uint32_t g_fn_count;

/* The SEH filters, handlers and the MSVCR80 establisher thunk: guest code
 * the Windows exception dispatcher calls at addresses that are not function
 * entries.  Lifted by scripts/lift_seh.py into lifter/runtime/seh_lifted.c. */
extern const uint32_t g_seh_fn_addr[];
extern const guest_fn_t g_seh_fn_ptr[];
extern const uint32_t g_seh_fn_count;

#define TEXT_BASE DISPATCH_TEXT_BASE
#define TEXT_SIZE DISPATCH_TEXT_SIZE

guest_fn_t g_dtab[DISPATCH_TEXT_SIZE];

#define DIAB_MAX 16384
uint32_t g_diab_addr[DIAB_MAX];
guest_fn_t g_diab_fn[DIAB_MAX];
uint32_t g_diab_count;
static int g_dtab_ready;
uint64_t g_stat_indirect_ok = 0, g_stat_indirect_miss = 0, g_stat_unsupported = 0;
uint64_t g_stat_win32_calls = 0, g_stat_win32_unimpl = 0;
/* A diabolus address with no lifted entry: the dispatcher emulates the
 * return instead of running the code.  Counted so the run report shows how
 * much real code was skipped rather than hiding it. */
uint64_t g_stat_diab_fallback = 0;

#define MISS_MAX 64
static uint32_t g_miss[MISS_MAX], g_miss_from[MISS_MAX];
static int g_miss_n;
static uint32_t g_unsup[MISS_MAX];
static int g_unsup_n;
static uint32_t g_diab_miss[MISS_MAX];
static int g_diab_miss_n;

/* The host-side backtrace is not used: guest_backtrace() walks the guest
 * stack instead, which is what actually matters here. */

void dispatch_init(void) {
    if (g_dtab_ready) return;
    g_dtab_ready = 1;
    uint32_t dup = 0, oob = 0;
    g_diab_count = 0;
    for (uint32_t i = 0; i < g_fn_count; i++) {
        uint32_t a = g_fn_addr[i];
        if (a - TEXT_BASE >= TEXT_SIZE) {
            /* Check if in diabolus range (0x10ea000-0x1aa8000) */
            if (a >= 0x10ea000 && a < 0x1aa8000) {
                if (g_diab_count < DIAB_MAX) {
                    g_diab_addr[g_diab_count] = a;
                    g_diab_fn[g_diab_count] = (guest_fn_t)g_fn_ptr[i];
                    g_diab_count++;
                } else {
                    oob++;
                }
            } else {
                oob++;
            }
        } else {
            if (g_dtab[a - TEXT_BASE]) dup++;
            g_dtab[a - TEXT_BASE] = (guest_fn_t)g_fn_ptr[i];
        }
    }
    /* Sort the diabolus table (addr and fn together) for binary search in
     * dispatch_indirect.  These are the RWX section's function entries: the
     * allocator's init (0x1a11e20) and friends live there, so they must run,
     * not be skipped. */
    for (uint32_t i = 1; i < g_diab_count; i++) {
        uint32_t ka = g_diab_addr[i];
        guest_fn_t kf = g_diab_fn[i];
        int32_t j = (int32_t)i - 1;
        while (j >= 0 && g_diab_addr[j] > ka) {
            g_diab_addr[j + 1] = g_diab_addr[j];
            g_diab_fn[j + 1] = g_diab_fn[j];
            j--;
        }
        g_diab_addr[j + 1] = ka;
        g_diab_fn[j + 1] = kf;
    }
    fprintf(stderr, "dispatch: %u functions registered (%u duplicates, %u out of range), %u diabolus functions\n",
            g_fn_count, dup, oob, g_diab_count);

    /* The SEH entry points live inside other functions and are not in the
     * function table; register them so the dispatcher can call them. */
    for (uint32_t i = 0; i < g_seh_fn_count; i++) {
        uint32_t a = g_seh_fn_addr[i];
        if (a - TEXT_BASE < TEXT_SIZE && !g_dtab[a - TEXT_BASE])
            g_dtab[a - TEXT_BASE] = (guest_fn_t)g_seh_fn_ptr[i];
    }
}

/* Lifted function at a diabolus-section address, or NULL.  The table is sorted
 * by dispatch_init(). */
static guest_fn_t diabolus_lookup(uint32_t a) {
    uint32_t lo = 0, hi = g_diab_count;
    while (lo < hi) {
        uint32_t m = (lo + hi) / 2;
        if (g_diab_addr[m] < a) lo = m + 1;
        else if (g_diab_addr[m] > a) hi = m;
        else return g_diab_fn[m];
    }
    return NULL;
}

static uint32_t nearest_fn(uint32_t a) {
    uint32_t lo = 0, hi = g_fn_count, best = 0;
    while (lo < hi) {
        uint32_t m = (lo + hi) / 2;
        if (g_fn_addr[m] <= a) { best = g_fn_addr[m]; lo = m + 1; }
        else hi = m;
    }
    return best;
}

uint32_t guest_fn_from_host(const void *pc) {
    uintptr_t p = (uintptr_t)pc, best = 0;
    uint32_t bestg = 0;
    for (uint32_t i = 0; i < g_fn_count; i++) {
        uintptr_t f = (uintptr_t)g_fn_ptr[i];
        if (f <= p && f > best) { best = f; bestg = g_fn_addr[i]; }
    }
    return bestg;
}

void guest_backtrace(CPU *c, int maxn) {
    fprintf(stderr, "    guest stack trace (esp=0x%08x):\n", c->esp);
    int shown = 0;
    for (uint32_t k = 0; k < 4096 && shown < maxn; k++) {
        uint32_t a = c->esp + 4u * k;
        if (!mem_is_committed(a)) continue;
        uint32_t v = MR32(a);
        if (v - TEXT_BASE < TEXT_SIZE) {
            uint32_t f = nearest_fn(v);
            fprintf(stderr, "      [%2d] +0x%04x  return 0x%08x   (sub_%08x + 0x%x)\n",
                    shown, k * 4, v, f, v - f);
            shown++;
        }
    }
}

static void unresolved(CPU *c, uint32_t addr) {
    g_stat_indirect_miss++;
    uint32_t ret = mem_is_committed(c->esp) ? MR32(c->esp) : 0;
    if (g_miss_n < MISS_MAX) { g_miss_from[g_miss_n] = ret; g_miss[g_miss_n++] = addr; }
    static int shown = 0;
    if (shown++ < 5) {
        fprintf(stderr, "\n  [UNRESOLVED INDIRECT CALL] target=0x%08x  return=0x%08x\n"
                        "      eax=%08x ecx=%08x edx=%08x ebx=%08x esi=%08x edi=%08x\n"
                        "      arg1=%08x arg2=%08x arg3=%08x\n",
                addr, ret, c->eax, c->ecx, c->edx, c->ebx, c->esi, c->edi,
                mem_is_committed(c->esp + 4) ? MR32(c->esp + 4) : 0,
                mem_is_committed(c->esp + 8) ? MR32(c->esp + 8) : 0,
                mem_is_committed(c->esp + 12) ? MR32(c->esp + 12) : 0);
        if (mem_is_committed(addr + 15)) {
            fprintf(stderr, "      96 bytes at the target:\n       ");
            for (int q = 0; q < 96; q++) {
                if (q && !(q & 15)) fprintf(stderr, "\n       ");
                fprintf(stderr, " %02x", MR8(addr + q));
            }
            fprintf(stderr, "\n");
        } else {
            fprintf(stderr, "      target NOT COMMITTED (garbage pointer)\n");
        }
        guest_backtrace(c, 12);
    }
    rtlog(RT_INDIRECT, addr, ret, c->esp);
    /* A wrong target means the lifted program has lost its way; stop at the
     * first one instead of running on with a corrupted state. */
    fprintf(stderr, "\n*** stopping: unresolved indirect call to 0x%08x\n", addr);
    win32_report();
    dispatch_report();
    exit(3);
}

void dispatch_indirect(CPU *c, uint32_t addr) {
    if (addr >= IMP_MAGIC) {
        uint32_t idx = IMP_INDEX(addr);
        if (idx < g_imp_count) {
            rtlog(RT_IMPORT, idx, mem_is_committed(c->esp) ? MR32(c->esp) : 0, 0);
            if (win32_call(c, idx) < 0) g_guest_exited = 1;
        } else {
            fprintf(stderr, "\n*** bad import index %u (addr %08x)\n", idx, addr);
            exit(3);
        }
        return;
    }
    uint32_t off = addr - TEXT_BASE;
    if (off < TEXT_SIZE) {
        guest_fn_t f = g_dtab[off];
        if (f) {
            g_stat_indirect_ok++;
            f(c);
            return;
        }
    }
    /* Diabolus section (0x10ea000-0x1aa8000): SecuROM's RWX code.  Its real
     * functions are lifted (the allocator's init, 0x1a11e20, is one); run
     * them.  An address we have no entry for is a block of a lifted function
     * reached by a `ret`, which the continuation seeds normally cover; if one
     * slips through, emulate the return the call site expects instead of
     * stopping, so the caller's own ret does not pop a stale address. */
    if (addr >= 0x10ea000 && addr < 0x1aa8000) {
        guest_fn_t f = diabolus_lookup(addr);
        if (f) {
            g_stat_indirect_ok++;
            f(c);
            return;
        }
        g_stat_indirect_ok++;
        g_stat_diab_fallback++;
        if (g_diab_miss_n < MISS_MAX) g_diab_miss[g_diab_miss_n++] = addr;
        if (mem_is_committed(c->esp)) POP32(c->eip);
        return;
    }
    unresolved(c, addr);
}

void dispatch_tail(CPU *c) {
    uint32_t a = c->eip;
    dispatch_indirect(c, a);
}

/* Call a guest function from the HLE: the lifted ABI returns through
 * POP32(eip), so put a sentinel on the guest stack and follow any tail
 * jumps until control comes back to it. */
int guest_call(CPU *c, uint32_t fn) {
    PUSH32(SENTINEL);
    uint32_t guard = 0;
    dispatch_indirect(c, fn);
    while (c->eip != SENTINEL) {
        uint32_t t = c->eip;
        c->eip = 0;
        if (++guard > 1000000u) {
            fprintf(stderr, "\n*** guest_call: tail-jump loop at 0x%08x\n", t);
            return 0;
        }
        dispatch_indirect(c, t);
    }
    return 1;
}

void unsupported_insn(CPU *c, uint32_t ip, const char *why) {
    (void)c;
    g_stat_unsupported++;
    if (g_unsup_n < MISS_MAX) g_unsup[g_unsup_n++] = ip;
    static int shown = 0;
    if (shown++ < 10)
        fprintf(stderr, "  [trap] unsupported instruction %s at 0x%08x\n", why, ip);
    rtlog(RT_TRAP, ip, 0, 0);
}

void cpuid_shim(CPU *c) {
    switch (c->eax) {
    case 0:
        c->eax = 1;
        c->ebx = 0x756e6547;  /* "Genu" */
        c->edx = 0x49656e69;  /* "ineI" */
        c->ecx = 0x6c65746e;  /* "ntel" */
        break;
    case 1:
        c->eax = 0x000006f8;   /* family 6, model 15, stepping 8 */
        c->ebx = 0x00000000;
        c->ecx = 0x00000000;
        c->edx = 0x078bfbff;   /* FPU, TSC, MMX, SSE, SSE2, ... */
        break;
    default:
        c->eax = c->ebx = c->ecx = c->edx = 0;
        break;
    }
}

uint64_t rdtsc_shim(void) {
    static uint64_t mhz = 0;
    if (!mhz) {
        const char *e = getenv("PROSTREET_TSC_MHZ");
        mhz = (e && *e) ? strtoull(e, 0, 0) : 3000u;
    }
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t ns = (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
    return ns * mhz / 1000ull;
}

void dispatch_report(void) {
    fprintf(stderr, "\n=== DISPATCH ===\n");
    fprintf(stderr, "  indirect calls resolved   : %llu\n", (unsigned long long)g_stat_indirect_ok);
    fprintf(stderr, "  indirect calls unresolved : %llu\n", (unsigned long long)g_stat_indirect_miss);
    fprintf(stderr, "  unsupported instructions  : %llu\n", (unsigned long long)g_stat_unsupported);
    if (g_stat_diab_fallback) {
        fprintf(stderr, "  diabolus skipped (no lift): %llu   first:",
                (unsigned long long)g_stat_diab_fallback);
        for (int i = 0; i < g_diab_miss_n && i < 10; i++)
            fprintf(stderr, " 0x%08x", g_diab_miss[i]);
        fprintf(stderr, "\n");
    }
    if (g_unsup_n) {
        fprintf(stderr, "  first trap addresses     :");
        for (int i = 0; i < g_unsup_n && i < 10; i++)
            fprintf(stderr, " 0x%08x", g_unsup[i]);
        fprintf(stderr, "\n");
    }
}

void rtlog_dump(int n) {
    uint64_t total = g_rtlog_n;
    int shown = 0;
    fprintf(stderr, "\n    --- last %d runtime events ---\n", n);
    for (uint64_t k = (total > (uint64_t)n ? total - (uint64_t)n : 0); k < total && shown < n; k++, shown++) {
        RtLog *e = &g_rtlog[k & (RTLOG_SIZE - 1u)];
        switch (e->kind) {
        case RT_IMPORT: {
            uint32_t i = e->a;
            if (i < g_imp_count)
                fprintf(stderr, "      import  %s!%s  ret=0x%08x\n",
                        g_imp[i].dll, g_imp[i].name, e->b);
            break;
        }
        case RT_INDIRECT:
            fprintf(stderr, "      indirect 0x%08x  ret=0x%08x\n", e->a, e->b);
            break;
        case RT_TRAP:
            fprintf(stderr, "      trap 0x%08x\n", e->a);
            break;
        default:
            fprintf(stderr, "      event %u 0x%08x\n", e->kind, e->a);
            break;
        }
    }
}

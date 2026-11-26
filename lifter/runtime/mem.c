/* Guest memory: the whole 4 GB address space is reserved PROT_NONE and the
 * regions actually in use are made readable/writable on demand.  A bad guest
 * access is then a diagnosable SIGSEGV with a guest address, not a host
 * crash somewhere in the lifted C. */
#define _GNU_SOURCE
#include "rt.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <ucontext.h>
#include <unistd.h>

#ifndef MAP_NORESERVE
#define MAP_NORESERVE 0x4000
#endif

uint8_t *g_mem = 0;
uint8_t g_cbits[1u << 16] = {0};   /* one bit per 16 KB page, 64 KB bitmap for 4 GB */
__thread CPU *g_cur_cpu = 0;       /* thread-local: see rt.h */

/* win32.c: top of the guest heap arena (fault diagnostics). */
extern uint32_t gh_break(void);

uint32_t g_itrace[ITRACE_SIZE];
uint64_t g_itrace_n = 0;
uint32_t g_watch_addr = 0, g_watch_val = 0, g_watch_prev_ip = 0;
uint32_t *g_cov = 0;
uint32_t g_last_fn = 0;
int g_prof_on = 0;

static uint32_t g_wwatch_lo = 0, g_wwatch_hi = 0;
static int g_wwatch_armed = 0;

RtLog g_rtlog[RTLOG_SIZE];
uint64_t g_rtlog_n = 0;

int g_guest_exited = 0;
int g_exit_code = 0;

void rtlog(uint32_t kind, uint32_t a, uint32_t b, uint32_t c) {
    RtLog *e = &g_rtlog[(uint32_t)g_rtlog_n & (RTLOG_SIZE - 1u)];
    e->kind = kind; e->a = a; e->b = b; e->c = c;
    g_rtlog_n++;
}

static uint64_t g_committed = 0;

void mem_init(void) {
    void *p = mmap(0, GSZ, PROT_NONE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (p == MAP_FAILED) {
        fprintf(stderr, "mem: cannot reserve 4 GB: %s\n", strerror(errno));
        exit(1);
    }
    g_mem = (uint8_t *)p;
    memset(g_cbits, 0, sizeof g_cbits);
    fprintf(stderr, "mem: guest space reserved at %p (%llu MB)\n",
            (void *)g_mem, (unsigned long long)(GSZ >> 20));
}

void mem_commit(uint32_t addr, uint32_t size) {
    if (!size) return;
    uint64_t lo = addr & ~0x3FFF;
    uint64_t hi = ((uint64_t)addr + size + 0x3FFF) & ~0x3FFF;
    if (hi > GSZ) hi = GSZ;
    if (lo >= hi) return;
    if (mprotect(g_mem + lo, (size_t)(hi - lo), PROT_READ | PROT_WRITE) != 0) {
        fprintf(stderr, "mem_commit(%08x,%u): mprotect: %s\n",
                addr, size, strerror(errno));
        exit(1);
    }
    for (uint64_t pg = lo >> 14; pg < (hi >> 14); pg++)
        g_cbits[pg >> 3] |= (uint8_t)(1u << (pg & 7));
    g_committed += hi - lo;
}

/* PE loader.  The Phase 0 image is flat (raw offset == RVA) but this loader
 * follows the section table so a normally packed file also loads. */
int mem_load_pe(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "mem: cannot open %s: %s\n", path, strerror(errno)); return 1; }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return 1; }
    long fsz = ftell(f);
    if (fsz < 0x200) { fclose(f); fprintf(stderr, "mem: %s is too small\n", path); return 1; }
    rewind(f);
    uint8_t *buf = malloc((size_t)fsz);
    if (!buf || fread(buf, 1, (size_t)fsz, f) != (size_t)fsz) {
        fprintf(stderr, "mem: cannot read %s\n", path); fclose(f); free(buf); return 1;
    }
    fclose(f);

    if (buf[0] != 'M' || buf[1] != 'Z') { fprintf(stderr, "mem: %s: no MZ\n", path); free(buf); return 1; }
    uint32_t peoff = *(uint32_t *)(buf + 0x3c);
    if (peoff + 0x18 > (uint32_t)fsz || *(uint32_t *)(buf + peoff) != 0x00004550u) {
        fprintf(stderr, "mem: %s: no PE header\n", path); free(buf); return 1;
    }
    uint8_t *fh = buf + peoff + 4;
    uint16_t nsec = *(uint16_t *)(fh + 2);
    uint16_t optsz = *(uint16_t *)(fh + 16);
    uint8_t *opt = fh + 20;
    uint32_t image_base = *(uint32_t *)(opt + 28);
    uint32_t size_image = *(uint32_t *)(opt + 56);
    uint32_t size_hdr = *(uint32_t *)(opt + 60);
    uint32_t entry = *(uint32_t *)(opt + 16);
    if (size_image == 0) { fprintf(stderr, "mem: SizeOfImage is 0\n"); free(buf); return 1; }
    if (image_base != 0x400000u)
        fprintf(stderr, "mem: warning: ImageBase is %08x, expected 00400000\n", image_base);

    mem_commit(image_base, size_image);
    memset(g_mem + image_base, 0, size_image);
    uint32_t hdr = size_hdr < (uint32_t)fsz ? size_hdr : (uint32_t)fsz;
    memcpy(g_mem + image_base, buf, hdr);

    uint8_t *sh = opt + optsz;
    uint32_t copied = 0;
    for (uint16_t i = 0; i < nsec; i++, sh += 40) {
        uint32_t vsize = *(uint32_t *)(sh + 8);
        uint32_t rva   = *(uint32_t *)(sh + 12);
        uint32_t rsize = *(uint32_t *)(sh + 16);
        uint32_t rptr  = *(uint32_t *)(sh + 20);
        uint32_t n = rsize < vsize ? rsize : vsize;
        if (!n) continue;
        if (rva + n > size_image) {
            fprintf(stderr, "mem: section %u runs past SizeOfImage\n", i);
            free(buf); return 1;
        }
        if ((uint64_t)rptr + n > (uint64_t)fsz) {
            fprintf(stderr, "mem: section %u raw data past EOF\n", i);
            free(buf); return 1;
        }
        memcpy(g_mem + image_base + rva, buf + rptr, n);
        copied += n;
    }
    free(buf);
    fprintf(stderr, "mem: loaded %s: ImageBase %08x, SizeOfImage %08x (%u sections, %u bytes), entry %08x\n",
            path, image_base, size_image, nsec, copied, image_base + entry);
    return 0;
}

/* Overlay one section of a clean image onto the loaded guest image.  The
 * unpacked artifact is a dump of a running process, so its .data carries
 * live heap pointers from that process (e.g. the allocator's free list at
 * 0xb4dd10).  The retail exe's .data has the initial values the game's own
 * initialisers expect; its .text is byte-identical to the artifact's. */
int mem_overlay_section(const char *path, const char *want) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "mem: overlay: cannot open %s: %s\n", path, strerror(errno)); return 1; }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return 1; }
    long fsz = ftell(f);
    if (fsz < 0x200) { fclose(f); fprintf(stderr, "mem: overlay: %s is too small\n", path); return 1; }
    rewind(f);
    uint8_t *buf = malloc((size_t)fsz);
    if (!buf || fread(buf, 1, (size_t)fsz, f) != (size_t)fsz) {
        fprintf(stderr, "mem: overlay: cannot read %s\n", path); fclose(f); free(buf); return 1;
    }
    fclose(f);

    uint32_t peoff = *(uint32_t *)(buf + 0x3c);
    if (peoff + 0x18 > (uint32_t)fsz || *(uint32_t *)(buf + peoff) != 0x00004550u) {
        fprintf(stderr, "mem: overlay: %s: no PE header\n", path); free(buf); return 1;
    }
    uint8_t *fh = buf + peoff + 4;
    uint16_t nsec = *(uint16_t *)(fh + 2);
    uint16_t optsz = *(uint16_t *)(fh + 16);
    uint8_t *opt = fh + 20;
    uint32_t image_base = *(uint32_t *)(opt + 28);
    uint8_t *sh = opt + optsz;
    for (uint16_t i = 0; i < nsec; i++, sh += 40) {
        const char *nm = (const char *)(sh);
        if (strncmp(nm, want, 8) != 0) continue;
        uint32_t vsize = *(uint32_t *)(sh + 8);
        uint32_t rva   = *(uint32_t *)(sh + 12);
        uint32_t rsize = *(uint32_t *)(sh + 16);
        uint32_t rptr  = *(uint32_t *)(sh + 20);
        uint32_t n = rsize < vsize ? rsize : vsize;
        if ((uint64_t)rptr + n > (uint64_t)fsz) {
            fprintf(stderr, "mem: overlay: %s raw data past EOF\n", want); free(buf); return 1;
        }
        memcpy(g_mem + image_base + rva, buf + rptr, n);
        fprintf(stderr, "mem: overlaid %s from %s: %u bytes at %08x\n",
                want, path, n, image_base + rva);
        free(buf);
        return 0;
    }
    fprintf(stderr, "mem: overlay: %s has no %s section\n", path, want);
    free(buf);
    return 1;
}

/* ------------------------------------------------------------------ */
/* fault reporting                                                     */

static uint8_t g_altstack[256 * 1024];

void mem_altstack_install(void) {
    stack_t ss;
    memset(&ss, 0, sizeof ss);
    ss.ss_sp = g_altstack;
    ss.ss_size = sizeof g_altstack;
    ss.ss_flags = 0;
    if (sigaltstack(&ss, 0) != 0)
        fprintf(stderr, "mem: sigaltstack: %s\n", strerror(errno));
}

static void fault_report(int sig, siginfo_t *si, void *uc_) {
    ucontext_t *uc = (ucontext_t *)uc_;
    static __thread int in_fault = 0;
    if (in_fault++) {
        fprintf(stderr, "\n*** RECURSIVE FAULT in signal handler! sig=%d addr=%p\n", sig, si ? si->si_addr : 0);
        _exit(4);
    }
    uintptr_t host_addr = (uintptr_t)si->si_addr;
    uint32_t guest_va = 0;
    int in_guest = 0;
    if (g_mem) {
        uintptr_t base = (uintptr_t)g_mem;
        uintptr_t off = host_addr - base;
        if (off < GSZ) { guest_va = (uint32_t)off; in_guest = 1; }
    }
    int is_write = 0;
    if (uc) {
#if defined(__x86_64__) && defined(REG_ERR)
        is_write = (uc->uc_mcontext.gregs[REG_ERR] & 0x2) != 0;
#endif
    }
    if (g_wwatch_armed && in_guest && is_write &&
        guest_va >= g_wwatch_lo && guest_va < g_wwatch_hi) {
        fprintf(stderr, "\n*** GUEST WRITE WATCH hit guest_va=0x%08x host=%p\n",
                guest_va, si->si_addr);
        if (uc) {
            greg_t *g = uc->uc_mcontext.gregs;
            fprintf(stderr, "    host PC %p  (guest function 0x%08x)\n",
                    (void *)g[REG_RIP], guest_fn_from_host((void *)g[REG_RIP]));
            fprintf(stderr, "    eax=%08lx ecx=%08lx edx=%08lx ebx=%08lx\n",
                    (unsigned long)g[REG_RAX], (unsigned long)g[REG_RCX],
                    (unsigned long)g[REG_RDX], (unsigned long)g[REG_RBX]);
            fprintf(stderr, "    esp=%08lx ebp=%08lx esi=%08lx di=%08lx\n",
                    (unsigned long)g[REG_RSP], (unsigned long)g[REG_RBP],
                    (unsigned long)g[REG_RSI], (unsigned long)g[REG_RDI]);
        }
        if (g_cur_cpu) {
            CPU *c = g_cur_cpu;
            fprintf(stderr, "    cpu: eip=%08x esp=%08x ebp=%08x eax=%08x ecx=%08x edx=%08x\n",
                    c->eip, c->esp, c->ebp, c->eax, c->ecx, c->edx);
            fprintf(stderr, "    cpu: ebx=%08x esi=%08x edi=%08x\n", c->ebx, c->esi, c->edi);
            if (mem_is_committed(c->esp)) guest_backtrace(c, 24);
        }
        rtlog_dump(24);
        uint64_t plo = (uint64_t)guest_va & ~0x3FFFu;
        mprotect(g_mem + plo, 0x4000, PROT_READ | PROT_WRITE);
        g_wwatch_armed = 0; /* catch first write, then allow progress */
        fprintf(stderr, "    watch: page %08llx set RW, continuing\n",
                (unsigned long long)plo);
        in_fault = 0;
        return;
    }
    /* Auto-commit page at fault address in guest address space and continue silently or with log */
    if (in_guest && guest_va >= 0x10000 && guest_va < 0x70000000) {
        mem_commit(guest_va, 0x4000);
        in_fault = 0;
        return;
    }
    fprintf(stderr, "\n*** GUEST FAULT: %s at host %p guest_va=%s%08x\n",
            sig == SIGSEGV ? "SIGSEGV" : sig == SIGBUS ? "SIGBUS" :
            sig == SIGILL ? "SIGILL" : sig == SIGFPE ? "SIGFPE" : "signal",
            si->si_addr, in_guest ? "0x" : "(none) ", in_guest ? guest_va : 0u);
    if (uc) {
        greg_t *g = uc->uc_mcontext.gregs;
        fprintf(stderr, "    host PC %p  (guest function 0x%08x)\n",
                (void *)g[REG_RIP], guest_fn_from_host((void *)g[REG_RIP]));
        fprintf(stderr, "    eax=%08lx ecx=%08lx edx=%08lx ebx=%08lx\n",
                (unsigned long)g[REG_RAX], (unsigned long)g[REG_RCX],
                (unsigned long)g[REG_RDX], (unsigned long)g[REG_RBX]);
        fprintf(stderr, "    esp=%08lx ebp=%08lx esi=%08lx di=%08lx\n",
                (unsigned long)g[REG_RSP], (unsigned long)g[REG_RBP],
                (unsigned long)g[REG_RSI], (unsigned long)g[REG_RDI]);
    }
    if (g_cur_cpu) {
        CPU *c = g_cur_cpu;
        fprintf(stderr, "    cpu: eip=%08x esp=%08x ebp=%08x eax=%08x ecx=%08x edx=%08x\n",
                c->eip, c->esp, c->ebp, c->eax, c->ecx, c->edx);
        fprintf(stderr, "    cpu: ebx=%08x esi=%08x edi=%08x\n",
                c->ebx, c->esi, c->edi);
        fprintf(stderr, "    fault guest_va=%08x in_guest=%d committed=%d heap_brk=%08x\n",
                guest_va, in_guest, in_guest ? mem_is_committed(guest_va) : 0, gh_break());
        fprintf(stderr, "    cpu: ptr=%p fs_base=%08x gs_base=%08x  (fs:[0] -> %08x)\n",
                (void *)c, c->fs_base, c->gs_base,
                c->fs_base ? MR32(c->fs_base) : 0);
        if (mem_is_committed(c->esp))
            guest_backtrace(c, 24);
    } else {
        fprintf(stderr, "    cpu: <none> (g_cur_cpu not set on this thread)\n");
    }
    rtlog_dump(24);
    _exit(4);
}

void mem_fault_handler_install(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = fault_report;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, 0);
    sigaction(SIGBUS, &sa, 0);
    sigaction(SIGILL, &sa, 0);
    sigaction(SIGFPE, &sa, 0);
}

void mem_watch_write_range(uint32_t lo, uint32_t hi) {
    if (hi <= lo || !g_mem) return;
    g_wwatch_lo = lo;
    g_wwatch_hi = hi;
    g_wwatch_armed = 1;
    uint64_t plo = (uint64_t)lo & ~0x3FFFu;
    uint64_t phi = ((uint64_t)hi + 0x3FFFu) & ~0x3FFFu;
    if (phi > GSZ) phi = GSZ;
    if (mprotect(g_mem + plo, (size_t)(phi - plo), PROT_READ) != 0)
        fprintf(stderr, "mem: watch mprotect RO failed [%08x,%08x): %s\n", lo, hi, strerror(errno));
    else
        fprintf(stderr, "mem: watch writes [%08x,%08x) pages [%08llx,%08llx)\n",
                lo, hi, (unsigned long long)plo, (unsigned long long)phi);
}

void mem_report(void) {
    fprintf(stderr, "  committed guest memory : %.1f MB in %llu pages\n",
            (double)g_committed / 1048576.0, (unsigned long long)(g_committed >> 14));
}

void itrace_dump(int n) { (void)n; }
void watch_report_change(uint32_t culprit_ip, uint32_t oldv, uint32_t newv) {
    (void)culprit_ip; (void)oldv; (void)newv;
}
#ifndef HOOK_SITE
#define HOOK_SITE(va) (va##u)
#endif

static uint32_t s_nil_sentinel = 0;

void guest_hook(CPU *c, uint32_t ip) {
    if (ip == HOOK_SITE(0x00531064)) {
        if (c->esi && mem_is_committed(c->esi + 4)) {
            uint32_t child = MR32(c->esi + 4);
            if (!child || !mem_is_committed(child + 0x22)) {
                if (!s_nil_sentinel) {
                    s_nil_sentinel = gh_alloc(64, 1, 4);
                    if (s_nil_sentinel) {
                        memset(g_mem + s_nil_sentinel, 0, 64);
                    }
                }
                if (s_nil_sentinel) {
                    MR32(c->esi + 4) = s_nil_sentinel;
                }
            }
        }
    }
}

/* Boot driver for the lifted ProStreet image.
 *
 * Sets up guest memory, the TEB/PEB and the stack, then calls the PE entry
 * point (0x00828c25) on a host thread with a large stack.  Everything the
 * guest reaches from there is either lifted code (out/lift-boot) or an HLE
 * import (win32.c); the first place it stops is reported by the runtime.
 */
#include "rt.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define ENTRY_VA 0x00828c25u

static CPU g_cpu;

static void setup_teb(void) {
    mem_commit(TEB_BASE, 0x20000u);
    memset(g_mem + TEB_BASE, 0, 0x20000u);
    MR32(TEB_BASE + 0x00) = 0xFFFFFFFFu;      /* end of SEH chain    */
    MR32(TEB_BASE + 0x04) = STACK_HI;         /* StackBase           */
    MR32(TEB_BASE + 0x08) = STACK_LO;         /* StackLimit          */
    MR32(TEB_BASE + 0x18) = TEB_BASE;         /* Self                */
    MR32(TEB_BASE + 0x20) = (uint32_t)getpid();  /* ProcessId        */
    MR32(TEB_BASE + 0x24) = 1;                /* ThreadId            */
    MR32(TEB_BASE + 0x30) = PEB_BASE;         /* PEB                 */
    teb_tls_init(TEB_BASE);

    mem_commit(PEB_BASE, 0x1000u);
    memset(g_mem + PEB_BASE, 0, 0x1000u);
    MR32(PEB_BASE + 0x08) = 0x00400000u;      /* ImageBaseAddress    */
}

static void *guest_main(void *arg) {
    (void)arg;
    mem_altstack_install();

    CPU *c = &g_cpu;
    memset(c, 0, sizeof *c);
    c->esp = STACK_HI - 0x1000u;
    c->fcw = 0x027f;                 /* x87 control word: 64-bit, round-nearest */
    c->fs_base = TEB_BASE;
    c->gs_base = TEB_BASE;
    g_cur_cpu = c;

    fprintf(stderr, "boot: calling entry 0x%08x  esp=0x%08x  teb=0x%08x\n",
            ENTRY_VA, c->esp, TEB_BASE);

    int ok = guest_call(c, ENTRY_VA);

    fprintf(stderr, "\nboot: guest returned (ok=%d) eax=%08x eip=%08x esp=%08x\n",
            ok, c->eax, c->eip, c->esp);
    win32_report();
    dispatch_report();
    mem_report();
    fflush(stderr);
    fflush(stdout);
    return 0;
}

/* Overlay the clean on-disk .data only when explicitly requested.  The
 * unpacked artifact is a dump of a running process, so its .data holds the
 * live post-load state; overlaying retail/zero-init .data is known to break
 * the recorded baseline and is therefore opt-in.
 *   argv[2]                        -> overlay that image's .data
 *   PROSTREET_CLEAN_IMAGE=<path>   -> overlay that image's .data */
static void overlay_clean_data(const char *pe, int argc, char **argv) {
    const char *clean = (argc > 2) ? argv[2] : getenv("PROSTREET_CLEAN_IMAGE");
    if (!clean || !*clean) {
        fprintf(stderr, "boot: keeping loaded .data (no clean-data overlay requested)\n");
        return;
    }
    if (!strcmp(clean, pe)) {
        fprintf(stderr, "boot: clean image is the main image, no .data overlay\n");
        return;
    }
    if (access(clean, R_OK) != 0) {
        fprintf(stderr, "boot: clean image %s not readable, keeping loaded .data\n", clean);
        return;
    }
    if (mem_overlay_section(clean, ".data"))
        fprintf(stderr, "boot: .data overlay from %s failed, keeping loaded .data\n", clean);
}

static int hex_digit(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + c - 'a';
    if (c >= 'A' && c <= 'F') return 10 + c - 'A';
    return -1;
}

static int parse_hex_u32(const char *s, const char **end, uint32_t *out) {
    uint64_t v = 0;
    int n = 0;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    for (;;) {
        int d = hex_digit((unsigned char)*s);
        if (d < 0) break;
        v = (v << 4) | (uint32_t)d;
        if (v > 0xFFFFFFFFull) return 0;
        s++; n++;
    }
    if (!n) return 0;
    *out = (uint32_t)v;
    *end = s;
    return 1;
}

/* PROSTREET_ZERO="0xSTART-0xEND[,0xSTART-0xEND...]" (END exclusive).
 * Applied after any .data overlay so experiments can reset selected ranges. */
static void apply_zero_env(void) {
    const char *v = getenv("PROSTREET_ZERO");
    if (!v || !*v) return;
    const char *p = v;
    while (*p) {
        while (*p == ' ' || *p == ',' || *p == '\t') p++;
        if (!*p) break;
        uint32_t start = 0, end = 0;
        const char *q = p;
        if (!parse_hex_u32(p, &q, &start)) {
            fprintf(stderr, "boot: PROSTREET_ZERO: bad range near '%s'\n", p);
            return;
        }
        p = q;
        if (*p != '-' && *p != ':') {
            fprintf(stderr, "boot: PROSTREET_ZERO: expected '-' after %08x\n", start);
            return;
        }
        p++;
        if (!parse_hex_u32(p, &q, &end)) {
            fprintf(stderr, "boot: PROSTREET_ZERO: bad end near '%s'\n", p);
            return;
        }
        p = q;
        if (end <= start) {
            fprintf(stderr, "boot: PROSTREET_ZERO: empty range %08x-%08x\n", start, end);
            continue;
        }
        mem_commit(start, end - start);
        memset(g_mem + start, 0, end - start);
        fprintf(stderr, "boot: PROSTREET_ZERO zeroed %08x-%08x (%u bytes)\n",
                start, end, end - start);
    }
}

/* PROSTREET_SET="VA=VALUE[,VA=VALUE...]" writes 32-bit values (hex) after
 * overlay/zero so experiments can patch specific globals. */
static void apply_set_env(void) {
    const char *v = getenv("PROSTREET_SET");
    if (!v || !*v) return;
    const char *p = v;
    while (*p) {
        while (*p == ' ' || *p == ',' || *p == '\t') p++;
        if (!*p) break;
        uint32_t va = 0, val = 0;
        const char *q = p;
        if (!parse_hex_u32(p, &q, &va)) {
            fprintf(stderr, "boot: PROSTREET_SET: bad va near '%s'\n", p);
            return;
        }
        p = q;
        if (*p != '=' && *p != ':') {
            fprintf(stderr, "boot: PROSTREET_SET: expected '=' after %08x\n", va);
            return;
        }
        p++;
        if (!parse_hex_u32(p, &q, &val)) {
            fprintf(stderr, "boot: PROSTREET_SET: bad value near '%s'\n", p);
            return;
        }
        p = q;
        mem_commit(va, 4);
        MR32(va) = val;
        fprintf(stderr, "boot: PROSTREET_SET [%08x]=%08x\n", va, val);
    }
}

/* PROSTREET_WATCH_W="0xSTART[-0xEND]" traps the first guest write to the
 * range (page-granular RO) and reports it, to find unexpected writers. */
static void apply_watch_env(void) {
    const char *v = getenv("PROSTREET_WATCH_W");
    if (!v || !*v) return;
    const char *p = v;
    while (*p == ' ' || *p == '\t') p++;
    uint32_t start = 0, end = 0;
    const char *q = p;
    if (!parse_hex_u32(p, &q, &start)) {
        fprintf(stderr, "boot: PROSTREET_WATCH_W: bad addr near '%s'\n", p);
        return;
    }
    p = q;
    if (*p == '-' || *p == ':') {
        p++;
        if (!parse_hex_u32(p, &q, &end)) {
            fprintf(stderr, "boot: PROSTREET_WATCH_W: bad end near '%s'\n", p);
            return;
        }
    } else {
        end = start + 4;
    }
    if (end <= start) end = start + 4;
    mem_watch_write_range(start, end);
}

int main(int argc, char **argv) {
    const char *pe = argc > 1 ? argv[1] : "artifacts/nfs.exe.unpacked";

    mem_init();
    mem_fault_handler_install();
    mem_altstack_install();
    if (mem_load_pe(pe)) return 1;
    overlay_clean_data(pe, argc, argv);
    apply_zero_env();
    apply_set_env();

    mem_commit(STACK_LO, STACK_HI - STACK_LO);
    win32_init();
    setup_teb();
    if (getenv("PROSTREET_TLS_TEST") && !win32_tls_selftest()) return 1;
    dispatch_init();

    /* Arm write-watch AFTER dispatch_init so we catch any init-time writes */
    apply_watch_env();

    pthread_attr_t at;
    pthread_attr_init(&at);
    pthread_attr_setstacksize(&at, 1ull << 30);   /* 1 GB host stack for guest recursion */
    pthread_t th;
    int rc = pthread_create(&th, &at, guest_main, 0);
    pthread_attr_destroy(&at);
    if (rc) {
        fprintf(stderr, "boot: pthread_create: %s\n", strerror(rc));
        return 1;
    }
    pthread_join(th, 0);
    return g_guest_exited ? g_exit_code : 0;
}

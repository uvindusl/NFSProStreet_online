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

    /* Initialize chunk descriptor list at 0x00ab47d4 (C++ constructor sub_0095aa70) */
    c->ecx = 0x00ab47d4u;
    guest_call(c, 0x0063da30u);
    c->esp = STACK_HI - 0x1000u;

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

/* Map the PE header page of the original game executable at the image base.
 *
 * SecuROM's computed calls and obfuscated constants mix image bytes into their
 * arithmetic, and some of those bytes are in the header page (for example
 * `mov esi,[0x19ee788]; add esi,[0x400148]`, the Fastmem size class requested
 * by sub_00421c90).  The unpacked artifact has a rebuilt header, so those
 * values come out wrong: that size class became 0xffb3f944 instead of 1 and
 * the game stopped with "Out of Fastmem memory".  The running process had the
 * headers of the on-disk executable there, so take them from it:
 *   PROSTREET_HEADER_IMAGE=<path>, else <gamedir>/nfsps.exe. */
static void overlay_original_headers(void) {
    char path[4096];
    const char *h = getenv("PROSTREET_HEADER_IMAGE");
    if (h && *h) {
        snprintf(path, sizeof path, "%s", h);
    } else {
        const char *d = getenv("PROSTREET_GAMEDIR");
        snprintf(path, sizeof path, "%s/nfsps.exe", (d && *d) ? d : "game");
    }
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "boot: no original executable at %s, keeping the artifact's PE headers "
                        "(SecuROM constants that read the header page will be wrong)\n", path);
        return;
    }
    uint8_t hdr[0x1000];
    size_t n = fread(hdr, 1, sizeof hdr, f);
    fclose(f);
    uint32_t peoff = n >= 0x40 ? *(uint32_t *)(hdr + 0x3c) : 0;
    if (n < 0x200 || hdr[0] != 'M' || hdr[1] != 'Z' || peoff + 0x78 > n ||
        *(uint32_t *)(hdr + peoff) != 0x00004550u) {
        fprintf(stderr, "boot: %s is not a PE image, keeping the artifact's PE headers\n", path);
        return;
    }
    uint8_t *opt = hdr + peoff + 24;
    uint32_t image_base = *(uint32_t *)(opt + 28);
    uint32_t size_hdr = *(uint32_t *)(opt + 60);
    if (image_base != 0x400000u || !mem_is_committed(image_base)) {
        fprintf(stderr, "boot: %s has ImageBase %08x, keeping the artifact's PE headers\n", path, image_base);
        return;
    }
    if (size_hdr > n) size_hdr = (uint32_t)n;
    memcpy(g_mem + image_base, hdr, size_hdr);
    fprintf(stderr, "boot: PE headers (%u bytes) taken from %s\n", size_hdr, path);
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
    overlay_original_headers();
    apply_zero_env();

    /* Initialize Pool descriptor circular sentinels to clean empty state */
    if (mem_is_committed(0x00b4dbf0)) {
        for (int i = 0; i < 32; i++) {
            uint32_t p = MR32(0x00b4dbf0 + i * 4);
            if (p && mem_is_committed(p)) {
                MR32(p + 0x04) = p + 0x04;
                MR32(p + 0x08) = p + 0x04;
                MR32(p + 0x0c) = p + 0x0c;
                MR32(p + 0x10) = p + 0x0c;
                MR32(p + 0x14) = p + 0x14;
                MR32(p + 0x18) = p + 0x14;
                MR32(p + 0x1c) = 0;
                MR32(p + 0x20) = 0;
                MR32(p + 0x24) = 0;
                MR32(p + 0x28) = 0;
                MR32(p + 0x2c) = 0;
                MR32(p + 0x30) = 0;
                MR32(p + 0x34) = 0;
                MR32(p + 0x38) = 0;
                MR32(p + 0x3c) = 0;
                MR32(p + 0x40) = 0xffffffff;
            }
        }
    }

    /* Stale pool pointer from dump; must be 0 for sub_006cd3b0 to initialize pool */
    if (mem_is_committed(0x00abafb0)) {
        MR32(0x00abafb0) = 0;
    }

    /* Stale singleton pointer from dump; must be 0 for sub_00701540 to initialize singleton */
    if (mem_is_committed(0x00ac107c)) {
        MR32(0x00ac107c) = 0;
    }

    /* Stale allocator/cache pointers from dump; must be 0 for sub_00530c60 to reinitialize */
    if (mem_is_committed(0x00ab0e5c)) {
        memset(g_mem + 0x00ab0e5c, 0, 0x24);
    }

    /* Circular list sentinels that must point to themselves when empty */
    if (mem_is_committed(0x00a61110)) {
        MR32(0x00a61110) = 0x00a61110;
        MR32(0x00a61114) = 0x00a61110;
    }
    if (mem_is_committed(0x00a61118)) {
        MR32(0x00a6111c) = 0x00a6111c;
        MR32(0x00a61120) = 0x00a6111c;
        MR32(0x00a61124) = 0x00a6111c;
        MR8(0x00a61128) = 1;
        MR8(0x00a61129) = 1;
        MR32(0x00a6112c) = 0;
    }
    if (mem_is_committed(0x00a61138)) {
        MR32(0x00a61138) = 0x00a61138;
        MR32(0x00a6113c) = 0x00a61138;
        MR32(0x00a61140) = 0x00a61138;
        MR8(0x00a61144) = 1;
        MR8(0x00a61145) = 1;
        MR32(0x00a61148) = 0;
    }
    if (mem_is_committed(0x00a61228)) {
        for (uint32_t a = 0x00a61228; a <= 0x00a61258; a += 8) {
            MR32(a) = a;
            MR32(a + 4) = a;
        }
    }
    if (mem_is_committed(0x00a79ba4)) {
        MR32(0x00a79ba4) = 0x00a79ba4;
        MR32(0x00a79ba8) = 0x00a79ba4;
    }
    if (mem_is_committed(0x00a9e18c)) {
        for (uint32_t a = 0x00a9e18c; a <= 0x00a9e1ac; a += 8) {
            MR32(a) = a;
            MR32(a + 4) = a;
        }
    }
    if (mem_is_committed(0x00a9e21c)) {
        MR32(0x00a9e21c) = 0x00a9e21c;
        MR32(0x00a9e220) = 0x00a9e21c;
    }
    if (mem_is_committed(0x00a9e08c)) {
        MR32(0x00a9e08c) = 0x00a9e08c;
        MR32(0x00a9e090) = 0x00a9e08c;
    }
    if (mem_is_committed(0x00a9e09c)) {
        MR32(0x00a9e09c) = 0x00a9e09c;
        MR32(0x00a9e0a0) = 0x00a9e09c;
    }
    if (mem_is_committed(0x00a9e0d4)) {
        MR32(0x00a9e0d4) = 0x00a9e0d4;
        MR32(0x00a9e0d8) = 0x00a9e0d4;
    }
    if (mem_is_committed(0x00a9e578)) {
        MR32(0x00a9e578) = 0x00a9e578;
        MR32(0x00a9e57c) = 0x00a9e578;
    }
    if (mem_is_committed(0x00a4f398)) {
        for (uint32_t a = 0x00a4f398; a <= 0x00a4f3a8; a += 8) {
            MR32(a) = a;
            MR32(a + 4) = a;
        }
    }
    if (mem_is_committed(0x00a610f8)) {
        MR32(0x00a610f8) = 0x00a610f8;
        MR32(0x00a610fc) = 0x00a610f8;
    }
    if (mem_is_committed(0x00a61110)) {
        MR32(0x00a61110) = 0x00a61110;
        MR32(0x00a61114) = 0x00a61110;
    }
    if (mem_is_committed(0x00a6111c)) {
        MR32(0x00a6111c) = 0x00a6111c;
        MR32(0x00a61120) = 0x00a6111c;
    }
    if (mem_is_committed(0x00a63cd0)) {
        MR32(0x00a63cd0) = 0x00a63cd0;
        MR32(0x00a63cd4) = 0x00a63cd0;
    }
    if (mem_is_committed(0x00a79b4c)) {
        MR32(0x00a79b4c) = 0x00a79b4c;
        MR32(0x00a79b50) = 0x00a79b4c;
    }
    if (mem_is_committed(0x00a79b80)) {
        MR32(0x00a79b80) = 0x00a79b80;
        MR32(0x00a79b84) = 0x00a79b80;
    }
    if (mem_is_committed(0x00a84990)) {
        MR32(0x00a84990) = 0x00a84990;
        MR32(0x00a84994) = 0x00a84990;
    }
    if (mem_is_committed(0x00b418d8)) {
        for (uint32_t a = 0x00b418d8; a <= 0x00b418e0; a += 8) {
            MR32(a) = a;
            MR32(a + 4) = a;
        }
    }

    /* Stale singleton pointers from process dump; must be 0 for lazy constructors to run */
    if (mem_is_committed(0x00b307e0)) {
        MR32(0x00b307e8) = 0;
        MR32(0x00b307f4) = 0;
        MR32(0x00b30814) = 0;
        MR32(0x00b3081c) = 0;
        MR32(0x00b30824) = 0;
        MR32(0x00b30838) = 0;
        MR32(0x00b30840) = 0;
        MR32(0x00b3085c) = 0;
        MR32(0x00b30944) = 0;
        MR32(0x00b30998) = 0;
        MR32(0x00b309ac) = 0;
        MR32(0x00b309b0) = 0;
        MR32(0x00b309b4) = 0;
        MR32(0x00b309c0) = 0;
    }
    if (mem_is_committed(0x00b340a0)) {
        MR32(0x00b340a0) = 0;
        MR32(0x00b340a4) = 0;
        MR32(0x00b340a8) = 0;
        MR32(0x00b340c8) = 0;
        MR32(0x00b340dc) = 0;
        MR32(0x00b340e0) = 0;
    }

    /* Stale std::map instances and static initialization guards from process dump */
    if (mem_is_committed(0x00acd9d0)) {
        memset(g_mem + 0x00acd9d0, 0, 0x40);
    }
    if (mem_is_committed(0x00aaeaf0)) MR32(0x00aaeaf0) = 0;
    if (mem_is_committed(0x00abab58)) MR32(0x00abab58) = 0;
    if (mem_is_committed(0x00ffd8f4)) MR32(0x00ffd8f4) = 0;
    if (mem_is_committed(0x00ac10c4)) MR32(0x00ac10c4) = 0;
    if (mem_is_committed(0x00ac10a4)) MR32(0x00ac10a4) = 0;
    if (mem_is_committed(0x00ab2898)) MR32(0x00ab2898) = 0;
    if (mem_is_committed(0x00bfbe6c)) MR32(0x00bfbe6c) = 0;
    if (mem_is_committed(0x00bfbea0)) MR32(0x00bfbea0) = 0;
    if (mem_is_committed(0x00bfbea4)) MR32(0x00bfbea4) = 0;
    if (mem_is_committed(0x00a7a474)) MR32(0x00a7a474) = 0;

    /* Stale streaming manager state from process dump */
    if (mem_is_committed(0x00a84938)) {
        MR32(0x00a84990) = 0;
        MR32(0x00a84994) = 0;
        MR32(0x00a84998) = 0;
        MR32(0x00a8499c) = 0;
        MR32(0x00a849a0) = 0;
    }

    /* Stale dynamic vertex stream table from process dump */
    if (mem_is_committed(0x00aa9638)) {
        memset(g_mem + 0x00aa9638, 0, 0x60);
        MR32(0x00aa9638) = 0xffffffff;
        MR32(0x00aa963c) = 0xffffffff;
        MR32(0x00aa9640) = 0xffffffff;
        MR32(0x00aa9644) = 0xffffffff;
        MR32(0x00aa9648) = 0xffffffff;
        MR32(0x00aa9684) = 0x00aa9638;
    }

    /* Stale chunk loader list and pool heads from process dump */
    if (mem_is_committed(0x00aa9620)) {
        MR32(0x00aa9620) = 0x00aa9620;
        MR32(0x00aa9624) = 0x00aa9620;
        MR32(0x00aa9628) = 0;
    }
    if (mem_is_committed(0x00abb060)) {
        for (uint32_t a = 0x00abb060; a <= 0x00abb09c; a += 4) {
            MR32(a) = 0;
        }
    }

    /* Stale stdext::hash_map and vector from process dump */
    if (mem_is_committed(0x00ff2520)) {
        if (mem_is_committed(0x00a99658)) {
            MR32(0x00a99658) = 0;
            MR32(0x00a9965c) = 0xffffffff;
        }
        MR32(0x00ff2524) = 0x00a99658;
        MR32(0x00ff2528) = 1;
        MR32(0x00ff252c) = 0;
        MR32(0x00ff2530) = 0x3f800000;
        MR32(0x00ff2534) = 0x40000000;
        MR32(0x00ff2538) = 0;
        MR32(0x00ff25d8) = 0;
        MR32(0x00ff25dc) = 0;
        MR32(0x00ff25e0) = 0;
        MR32(0x00ff25cc) = 0;
        MR32(0x00ff25d0) = 0;
        MR32(0x00ff25d4) = 0;
        MR32(0x00ff2614) = 0;
        MR32(0x00ff2618) = 0x00ff2618;
        MR32(0x00ff261c) = 0x00ff2618;
        MR32(0x00ff2620) = 0;
        MR32(0x00ff2628) = 0;
        MR32(0x00ff262c) = 0;
    }

    /* Stale allocator tracking list head from process dump */
    if (mem_is_committed(0x00a9dee8)) {
        MR32(0x00a9dee8) = 0;
        MR32(0x00a9deec) = 0x00a9deec;
        MR32(0x00a9def0) = 0x00a9deec;
    }

    /* Stale file/asset manager state from process dump (0x00a61110..0x00a61248) */
    if (mem_is_committed(0x00a61110)) {
        MR32(0x00a61110) = 0x00a61110;
        MR32(0x00a61114) = 0x00a61110;
        MR32(0x00a6111c) = 0x00a6111c;
        MR32(0x00a61120) = 0x00a6111c;
        MR32(0x00a61124) = 0;
        MR32(0x00a61128) = 0;
        MR32(0x00a61138) = 0x00a61138;
        MR32(0x00a6113c) = 0x00a61138;
        MR32(0x00a61140) = 0;
        MR32(0x00a61144) = 0;
        MR32(0x00a61240) = 0x00a61240;
        MR32(0x00a61244) = 0x00a61240;
    }

    /* Stale render command queue from process dump (0x00ffc9f0: queued count,
     * executed count, enabled, capacity, base, write ptr, read ptr).  The dump
     * was taken mid-frame with commands still pending, so the render thread
     * would replay draws against buffers this run has not created yet.  Mark
     * the queue empty. */
    if (mem_is_committed(0x00ffc9f0)) {
        uint32_t base = MR32(0x00ffca00);
        MR32(0x00ffc9f0) = 0;
        MR32(0x00ffc9f4) = 0;
        MR32(0x00ffca04) = base;
        MR32(0x00ffca08) = base;
    }

    apply_set_env();

    mem_commit(STACK_LO, STACK_HI - STACK_LO);
    win32_init();

    /* Stale std::list head nodes from process dump (lists at 0x00ff2600 and
     * 0x00ff260c: {alloc, head, size}).  The head nodes lived in the dumped
     * process heap, which is not part of the image, so the first insert
     * (sub_00507ed4 fills the second list with its 100 entries) linked through
     * a null prev pointer.  Give each list a fresh empty head. */
    if (mem_is_committed(0x00ff2600)) {
        static const uint32_t lists[] = { 0x00ff2600, 0x00ff260c };
        for (unsigned i = 0; i < sizeof lists / sizeof *lists; i++) {
            uint32_t head = gh_alloc(0x20, 1, 16);
            MR32(head) = head;
            MR32(head + 4) = head;
            MR32(lists[i] + 4) = head;
            MR32(lists[i] + 8) = 0;
        }
    }

    /* Initialize DisplayDevice and RenderParams singletons */
    if (mem_is_committed(0x00ac6ed4)) {
        uint32_t dev = MR32(0x00ac6ed4);
        if (dev) {
            mem_commit(dev, 0x2000);
            extern uint32_t com_get_device(void);
            uint32_t dev_obj = com_get_device();
            MR32(dev) = dev_obj;
            if (mem_is_committed(0x00bfc10c)) MR32(0x00bfc10c) = dev_obj;
            MR32(dev + 0x180) = 0x15;
            MR32(dev + 0x184) = 0xffffffff;
            MR8(dev + 0x1a8) = 1;
            MR32(dev + 0x1ac) = 5;
            uint32_t cube = gh_alloc(0x40, 1, 16);
            MR32(dev + 0x1a4) = cube;
            fprintf(stderr, "boot: DisplayDevice 0x%08x initialized (pDev=0x%08x, 0xbfc10c=0x%08x), cube_struct=0x%08x\n",
                    dev, dev_obj, MR32(0x00bfc10c), cube);
        }
    }
    if (mem_is_committed(0x00ac6f14)) {
        uint32_t rp = MR32(0x00ac6f14);
        if (rp) {
            mem_commit(rp, 0x84);
            if (mem_is_committed(0x00990598)) {
                memcpy(g_mem + rp, g_mem + 0x00990598, 0x84);
                fprintf(stderr, "boot: RenderParams 0x%08x initialized from template 0x00990598\n", rp);
            }
        }
    }

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

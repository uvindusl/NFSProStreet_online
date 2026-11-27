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

void null_dead_heap_pointers(void);

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

    null_dead_heap_pointers();

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
static void original_exe_path(char *path, size_t n) {
    const char *h = getenv("PROSTREET_HEADER_IMAGE");
    if (h && *h) {
        snprintf(path, n, "%s", h);
    } else {
        const char *d = getenv("PROSTREET_GAMEDIR");
        snprintf(path, n, "%s/nfsps.exe", (d && *d) ? d : "game");
    }
}

static void overlay_original_headers(void) {
    char path[4096];
    original_exe_path(path, sizeof path);
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

/* The original executable's .data, kept from repair_stale_lists() until
 * null_dead_heap_pointers() has run. */
static uint8_t *g_orig_data;
static uint32_t g_orig_lo, g_orig_size;

/* Relink the list headed at `a` to the nodes that are static objects in
 * [lo,hi).  Returns 0 if the list was intact (or empty), 1 if it is now
 * empty, 2 if it kept static nodes. */
static int relink_list(uint32_t a, uint32_t lo, uint32_t hi) {
#define IN_DATA(p) ((p) >= lo && (p) + 8u <= hi)
    enum { MAXN = 4096 };
    static uint32_t fwd[MAXN], bwd[MAXN];
    if (MR32(a) == a && MR32(a + 4) == a) return 0;
    unsigned nf = 0, nb = 0;
    uint32_t p = MR32(a);
    while (p != a && IN_DATA(p) && nf < MAXN) { fwd[nf++] = p; p = MR32(p); }
    if (p == a) return 0;               /* every node is in the image: the list is intact */
    p = MR32(a + 4);
    while (p != a && IN_DATA(p) && nb < MAXN) {
        unsigned k = 0;
        while (k < nf && fwd[k] != p) k++;
        if (k < nf) break;
        bwd[nb++] = p; p = MR32(p + 4);
    }
    /* head -> fwd[0..] -> bwd[nb-1..0] -> head */
    uint32_t prev = a;
    for (unsigned k = 0; k < nf + nb; k++) {
        uint32_t node = k < nf ? fwd[k] : bwd[nb - 1 - (k - nf)];
        MR32(prev) = node;
        MR32(node + 4) = prev;
        prev = node;
    }
    MR32(prev) = a;
    MR32(a + 4) = prev;
    return (nf + nb) ? 2 : 1;
#undef IN_DATA
}

/* Repair the intrusive lists whose chain leaves the image.
 *
 * The artifact's .data is a snapshot of a running process, so a list head
 * there can link to nodes in that process's heap, which the image does not
 * contain.  Walking such a list reads whatever this run has at those
 * addresses (sub_00455110 faulted on the list at 0x00a9e04c).  The original
 * executable tells us where the list heads are: an empty head is two
 * pointers to itself, {X, X} at X.  For each one that is not empty in the
 * dump, keep the nodes that are static objects in .data (they were linked by
 * static constructors, which do not run again) and drop the rest. */
static void repair_stale_lists(void) {
    char path[4096];
    original_exe_path(path, sizeof path);
    FILE *f = fopen(path, "rb");
    if (!f) return;                         /* overlay_original_headers already said so */
    uint8_t hdr[0x1000];
    size_t n = fread(hdr, 1, sizeof hdr, f);
    uint32_t peoff = n >= 0x40 ? *(uint32_t *)(hdr + 0x3c) : 0;
    if (n < 0x200 || hdr[0] != 'M' || hdr[1] != 'Z' || peoff + 0x100 > n) { fclose(f); return; }
    uint16_t nsec = *(uint16_t *)(hdr + peoff + 6);
    uint8_t *sh = hdr + peoff + 24 + *(uint16_t *)(hdr + peoff + 20);
    uint32_t lo = 0, size = 0, raw = 0;
    for (uint16_t i = 0; i < nsec && (size_t)(sh - hdr) + 40 <= n; i++, sh += 40) {
        if (!memcmp(sh, ".data\0\0\0", 8)) {
            uint32_t vsize = *(uint32_t *)(sh + 8), rsize = *(uint32_t *)(sh + 16);
            lo = 0x400000u + *(uint32_t *)(sh + 12);
            size = rsize < vsize ? rsize : vsize;
            raw = *(uint32_t *)(sh + 20);
        }
    }
    uint8_t *orig = size ? malloc(size) : NULL;
    if (!orig || fseek(f, (long)raw, SEEK_SET) != 0 || fread(orig, 1, size, f) != size ||
        !mem_is_committed(lo) || !mem_is_committed(lo + size - 1)) {
        fclose(f); free(orig); return;
    }
    fclose(f);
    uint32_t hi = lo + size;
    unsigned heads = 0, emptied = 0, relinked = 0;
    for (uint32_t a = lo; a + 8u <= hi; a += 4) {
        if (*(uint32_t *)(orig + (a - lo)) != a || *(uint32_t *)(orig + (a - lo) + 4) != a) continue;
        heads++;
        int r = relink_list(a, lo, hi);
        if (r == 1) emptied++; else if (r == 2) relinked++;
    }
    g_orig_data = orig; g_orig_lo = lo; g_orig_size = size;

    /* Heads that a static constructor links to themselves are zero in the
     * original executable, so the scan above cannot see them.  Code can: a
     * walk loads the head and compares the result with the head's own address
     * (`mov r,[X]; ...; cmp r,X`).  sub_0077def0 walks the one at 0x00ffad24. */
    unsigned heads2 = 0;
    const uint32_t text_lo = 0x00401000u, text_hi = 0x00401000u + 0x0056507bu;
    for (uint32_t t = text_lo; t + 32u < text_hi; t++) {
        uint32_t x, reg, n;
        if (MR8(t) == 0xA1) { reg = 0; x = MR32(t + 1); n = 5; }
        else if (MR8(t) == 0x8B && (MR8(t + 1) & 0xC7) == 0x05) { reg = (MR8(t + 1) >> 3) & 7; x = MR32(t + 2); n = 6; }
        else continue;
        if (x < lo || x + 8u > hi || (x & 3u)) continue;
        int cmp = 0;
        for (uint32_t k = t + n; k < t + n + 16u && !cmp; k++) {
            cmp = (MR8(k) == 0x81 && MR8(k + 1) == 0xF8 + reg && MR32(k + 2) == x) ||
                  (reg == 0 && MR8(k) == 0x3D && MR32(k + 1) == x);
        }
        if (!cmp || MR32(x) == 0 || MR32(x) == x) continue;
        int r = relink_list(x, lo, hi);
        if (r) { heads2++; if (r == 1) emptied++; else relinked++; }
    }
    /* Arrays of heads (one list per bucket or per priority) are reached
     * through a register, so neither scan sees their members; an empty member
     * gives the array away.  Next to a self-linked head, a pair of pointers
     * into the dumped heap at the same 8-byte stride is another head of that
     * array (0x00ae3448 and 0x00ae3450, beside the empty 0x00ae3460, are walked
     * by sub_007476d0). */
#define STALE(v) ((v) >= 0x01aa8000u && (v) < 0x40000000u && !((v) & 3u))
    for (uint32_t a = lo + 8u; a + 16u <= hi; a += 4) {
        if (MR32(a) != a || MR32(a + 4) != a) continue;
        for (int dir = -8; dir <= 8; dir += 16) {
            for (uint32_t b = a + (uint32_t)dir; b >= lo && b + 8u <= hi; b += (uint32_t)dir) {
                if (!STALE(MR32(b)) || !STALE(MR32(b + 4))) break;
                int r = relink_list(b, lo, hi);
                if (!r) break;
                heads2++; if (r == 1) emptied++; else relinked++;
            }
        }
    }
#undef STALE
    fprintf(stderr, "boot: %u list heads in .data, %u pointed into the dumped heap: %u emptied, "
                    "%u relinked to their static nodes\n", heads + heads2, emptied + relinked, emptied, relinked);
}

/* Drop the elements of inline pointer vectors that point into the dumped heap.
 *
 * The game's fixed-capacity vector is {vtable, begin, capacity, count} with
 * `begin` pointing at the storage that follows it.  The dump has 79 of them in
 * .data, and some still list objects of the dumped process (render-target
 * registrations, views): sub_004c91d0 walks the one at 0x00ace440 and faulted
 * calling through a dead object.  Nothing has been allocated yet when this
 * runs, so a heap-range element can only be stale.  A vector is touched only
 * if every element is a pointer (into the image, or stale); vectors of other
 * element types are left alone. */
static void repair_stale_vectors(void) {
    const uint32_t rdata_lo = 0x00967000u, data_lo = 0x00a4a000u, data_hi = 0x00a4a000u + 0x005c54dcu;
    const uint32_t image_hi = 0x01aa8000u;
    if (!mem_is_committed(data_lo) || !mem_is_committed(data_hi - 1)) return;
    unsigned vectors = 0, fixed = 0, dropped = 0;
    for (uint32_t a = data_lo; a + 16u <= data_hi; a += 4) {
        uint32_t vt = MR32(a), begin = MR32(a + 4), cap = MR32(a + 8), count = MR32(a + 12);
        if (vt < rdata_lo || vt >= data_lo || begin != a + 16u || !cap || cap > 4096u ||
            count > cap || begin + 4u * cap > data_hi) continue;
        vectors++;
        unsigned stale = 0, other = 0;
        for (uint32_t i = 0; i < count; i++) {
            uint32_t e = MR32(begin + 4u * i);
            if (e >= 0x00400000u && e < image_hi) continue;
            if (e >= image_hi && e < 0x40000000u && !(e & 3u)) stale++; else other++;
        }
        if (!stale || other) continue;
        uint32_t keep = 0;
        for (uint32_t i = 0; i < count; i++) {
            uint32_t e = MR32(begin + 4u * i);
            if (e < image_hi) MR32(begin + 4u * keep++) = e;
        }
        for (uint32_t i = keep; i < count; i++) MR32(begin + 4u * i) = 0;
        MR32(a + 12) = keep;
        fixed++; dropped += stale;
    }
    fprintf(stderr, "boot: %u inline vectors in .data, %u listed objects of the dumped heap (%u entries dropped)\n",
            vectors, fixed, dropped);
}

static int hex_digit(int c);
static int parse_hex_u32(const char *s, const char **end, uint32_t *out);

/* Null the words of .data that are zero in the original executable and hold
 * an address in the dumped process's heap (singletons, caches, loaded-asset
 * pointers): that heap is not part of the image, so the game finds "already
 * created" objects that are all zeros.
 *
 * Doing this for all of .data (about 6,700 words) breaks the boot before the
 * window opens, so it is an experiment switch, not the default:
 *   PROSTREET_NULL_STALE="0xLO-0xHI[,0xLO-0xHI...]"   ranges of .data to treat
 * A pointer is kept when its target has content (boot code that builds an
 * object at the dumped address runs before this). */
void null_dead_heap_pointers(void) {
    const char *v = getenv("PROSTREET_NULL_STALE");
    if (g_orig_data && v && *v) {
        uint32_t lo = g_orig_lo, hi = g_orig_lo + g_orig_size;
        const char *p = v;
        while (*p) {
            while (*p == ' ' || *p == ',') p++;
            uint32_t r0 = 0, r1 = 0; const char *q = p;
            if (!parse_hex_u32(p, &q, &r0) || *q != '-' || !parse_hex_u32(q + 1, &q, &r1)) break;
            p = q;
            if (r0 < lo) r0 = lo;
            if (r1 > hi) r1 = hi;
            unsigned nulled = 0, kept = 0;
            for (uint32_t a = (r0 + 3u) & ~3u; a + 4u <= r1; a += 4) {
                uint32_t w = MR32(a);
                if (w < 0x01aa8000u || w >= 0x30000000u || (w & 3u)) continue;
                if (*(uint32_t *)(g_orig_data + (a - lo)) != 0) continue;
                int live = 0;
                for (uint32_t k = 0; k < 64u && !live; k += 4)
                    live = mem_is_committed(w + k) && MR32(w + k) != 0;
                if (live) { kept++; continue; }
                MR32(a) = 0;
                nulled++;
            }
            fprintf(stderr, "boot: PROSTREET_NULL_STALE %08x-%08x: %u dead heap pointers nulled, %u kept\n",
                    r0, r1, nulled, kept);
        }
    }
    free(g_orig_data);
    g_orig_data = NULL;
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
    repair_stale_lists();
    repair_stale_vectors();
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

    /* Stale singleton pointers from process dump, found one fault at a time
     * during render init (sub_004b4090).  Each is tested for null before use,
     * so null is the state the game's own start-up expects.  (Nulling every
     * pointer into the dumped heap, about 6,700 words of .data, was tried and
     * breaks the boot long before the window opens: much of that state is
     * relied on.)
     *   0x00ab27dc  object whose vtable+0x74 sub_00597140 calls
     *   0x00a83bcc  geometry of the static model at 0x00a83bc0 (sub_004be090) */
    {
        static const uint32_t dead[] = { 0x00ab27dcu, 0x00a83bccu };
        for (unsigned i = 0; i < sizeof dead / sizeof *dead; i++)
            if (mem_is_committed(dead[i])) MR32(dead[i]) = 0;
    }

    apply_set_env();

    mem_commit(STACK_LO, STACK_HI - STACK_LO);
    win32_init();

    /* Stale std::list head nodes from process dump (four lists in a row,
     * {alloc, head, size} at 0x00ff25e8, 0x00ff25f4, 0x00ff2600, 0x00ff260c).
     * The head nodes lived in the dumped process heap, which is not part of
     * the image, so the first insert linked through a null prev pointer
     * (sub_00507ed4 fills the last list with its 100 entries; sub_004a34e0
     * uses the first one each frame).  Give each list a fresh empty head. */
    if (mem_is_committed(0x00ff2600)) {
        static const uint32_t lists[] = { 0x00ff25e8, 0x00ff25f4, 0x00ff2600, 0x00ff260c };
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

/* Fixes for bugs in the original game (not in this runtime), applied as
 * guest hooks: the lifted code calls guest_hook(c, ip) before each
 * HOOK_SITE instruction (scripts/lift_boot.sh collects the sites; a new one
 * needs a re-lift).  Each fix names the community patch that documents it.
 * PROSTREET_GAMEFIXES=0 turns them all off. */
#include <stdio.h>
#include <stdlib.h>
#include "cpu.h"

#ifndef HOOK_SITE
#define HOOK_SITE(va) (va##u)
#endif
#ifndef HOOK_RETURN_SITE
#define HOOK_RETURN_SITE(va) (va##u)
#endif

static int fixes_on(void) {
    static int on = -1;
    if (on < 0) { const char *e = getenv("PROSTREET_GAMEFIXES"); on = !(e && *e == '0'); }
    return on;
}

int gamefix_hook(CPU *c, uint32_t ip) {
    switch (ip) {
    /* TrackStreamer::HandleLoading (sub_00748820): for one section (2500) it
     * sets the local flag [esp+0x13] and then allocates the section from the
     * main pool (0x748a10, sub_00435e90), which runs out: the game reports a
     * fatal error and, in this runtime, faults in its error path
     * (sub_006d2c70) when a race loads.  ThirteenAG's NFS ProStreet FusionFix
     * removes both tests of the flag (NOP 0x7489fd, jmp 0x748a09 -> 0x748a20)
     * so every section takes the streamer's own pool; the flag is read only
     * by these two tests, so clearing it before each does the same. */
    case HOOK_SITE(0x007489f8):
    case HOOK_SITE(0x00748a09):
        if (fixes_on()) MR8(c->esp + 0x13) = 0;
        return 1;
    /* Not a game bug: a dumped-process leftover.  The main pool's free
     * (sub_00436130) looks a block up in the external regions (0x00b4d9fc,
     * 16-byte slots to 0x00b4dbfc: region at +8, length at +0xc) and
     * otherwise expects its own block header (magic byte 0x22 at -0xb); it
     * asserts (mov [0],ebx) when neither holds.  Blocks the dumped process
     * allocated have neither in this run (their memory was not in the dump),
     * and statics still pointing at them get freed when a race loads.  Such a
     * free is skipped: the routine's own NULL test returns early. */
    case HOOK_SITE(0x00436147): {
        uint32_t p = c->esi;
        if (!p || !mem_is_committed(p - 0x14)) return 1;
        for (uint32_t slot = 0x00b4d9fcu; slot < 0x00b4dbfcu; slot += 0x10) {
            uint32_t r = MR32(slot);
            if (r && mem_is_committed(r + 0x10) && p >= MR32(r + 8) && p - MR32(r + 8) < MR32(r + 0xc)) return 1;
        }
        if (MR8(p - 0xb) == 0x22) return 1;
        static unsigned n;
        if (n++ < 20) fprintf(stderr, "pool: free of %#x skipped (no block header: a dumped-process block)\n", p);
        c->esi = 0;
        return 1;
    }
    /* Not a fix: the pool allocator's out-of-memory report (sub_00435eb0
     * prints it through sub_004324e0 and then asserts); the game's own
     * output goes nowhere here, so print it. */
    case HOOK_SITE(0x004360fc): {
        uint32_t sp = c->esp;   /* fmt, pool name, 0, what, size... pushed in reverse */
        const char *pool = mem_is_committed(MR32(sp + 4)) ? (const char *)g_mem + MR32(sp + 4) : "?";
        fprintf(stderr, "pool: ERROR out of memory in pool %.40s: args %#x %#x %#x %#x (total? %u)\n",
                pool, MR32(sp + 8), MR32(sp + 12), MR32(sp + 16), MR32(sp + 20), MR32(sp + 24));
        return 1;
    }
    /* Not a fix: the chunk loader (sub_006d2c70) asserts with mov [0],0 when
     * no handler accepts a chunk; say which chunk before it does. */
    case HOOK_SITE(0x006d2d23):
        fprintf(stderr, "chunk loader: no handler for chunk %08x (size %#x) at %#x\n",
                mem_is_committed(c->edi) ? MR32(c->edi) : 0, mem_is_committed(c->edi + 4) ? MR32(c->edi + 4) : 0, c->edi);
        {   /* the buffer being parsed: sub_006d2c70(ptr, size, section) */
            uint32_t p = MR32(c->esp + 0x118), n = MR32(c->esp + 0x11c), sec = MR32(c->esp + 0x120);
            fprintf(stderr, "chunk loader: buffer %#x size %#x; first words:", p, n);
            for (uint32_t k = 0; k < 8 && mem_is_committed(p + 4 * k); k++) fprintf(stderr, " %08x", MR32(p + 4 * k));
            fprintf(stderr, "\nchunk loader: section %#x:", sec);
            for (uint32_t k = 0; k < 24 && mem_is_committed(sec + 4 * k); k++) fprintf(stderr, " %08x", MR32(sec + 4 * k));
            fprintf(stderr, "\n");
        }
        return 1;
    }
    return 0;
}

/* Sites where the hook may finish the function (HOOK_RETURN_SITE; the lifted
 * code returns when this returns nonzero, after the hook popped the return
 * address and set eax). */
int gamefix_hook_ret(CPU *c, uint32_t ip) {
    switch (ip) {
    /* EA's audio engine runs each voice's AEMS module chain here
     * (sub_008b2cb0, thiscall, 3 stack arguments; returns 1 to go on with the
     * voice's second stage, sub_008b5cf0).  The modules are x86 the game
     * loads into its heap and this runtime does not run yet
     * (foreign_code_call), so the chain tables hold garbage once a race
     * starts its engine sounds.  Until the modules are lifted the voice
     * produces nothing: return 0 at once.  PROSTREET_AUDIO_VOICES=1 runs the
     * real code. */
    case HOOK_RETURN_SITE(0x008b2cb0): {
        static int run = -1;
        if (run < 0) { const char *e = getenv("PROSTREET_AUDIO_VOICES"); run = e && *e == '1'; }
        if (run) {
            /* sub_008b2cb0(ecx voice, table, list entry, chain): log each new
             * chain's module table once (PROSTREET_AUDIO_TRACE=1) */
            static int trace = -1; static uint32_t seen[64]; static unsigned nseen;
            if (trace < 0) { const char *e = getenv("PROSTREET_AUDIO_TRACE"); trace = e && *e == '1'; }
            uint32_t tab = MR32(c->esp + 4), chain = MR32(c->esp + 12);
            unsigned i;
            for (i = 0; i < nseen && seen[i] != chain; i++) ;
            if (trace && i == nseen && nseen < 64 && mem_range_committed(chain, 0x80)) {
                seen[nseen++] = chain;
                unsigned n = MR8(chain + 0x46);
                fprintf(stderr, "voices: chain %#x table %#x count %u/%u [+0x18]=%#x:", chain, tab, MR8(chain + 0x45), n, MR32(chain + 0x18));
                for (unsigned k = 0; k <= n && k < 8 && mem_range_committed(tab + 12 * k, 12); k++)
                    fprintf(stderr, " {%08x %08x %08x}", MR32(tab + 12 * k), MR32(tab + 12 * k + 4), MR32(tab + 12 * k + 8));
                fprintf(stderr, " inst:");
                for (unsigned k = 0; k <= n && k < 8; k++) fprintf(stderr, " %08x", MR32(chain + 0x4c + 4 * k));
                fprintf(stderr, "\n");
            }
            return 0;
        }
        POP32(c->eip);
        c->esp += 0xc;
        c->eax = 0;
        return 1;
    }
    }
    return 0;
}

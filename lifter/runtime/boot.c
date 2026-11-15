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

/* The unpacked artifact is a dump of a running process, so its .data holds
 * live pointers from that process (the allocator's free list, for one).  The
 * retail exe has the initial .data the game expects, and its .text is
 * byte-identical, so load the artifact for its layout and put the clean
 * .data back.  A different main image needs no overlay. */
static void overlay_clean_data(const char *pe, int argc, char **argv) {
    const char *clean = argc > 2 ? argv[2] : getenv("PROSTREET_CLEAN_IMAGE");
    if (!clean || !*clean) clean = "game/nfsps.exe";
    if (!strcmp(clean, pe)) return;
    if (access(clean, R_OK) != 0) {
        fprintf(stderr, "boot: no clean image at %s, keeping loaded .data\n", clean);
        return;
    }
    if (mem_overlay_section(clean, ".data"))
        fprintf(stderr, "boot: .data overlay from %s failed, keeping loaded .data\n", clean);
}

int main(int argc, char **argv) {
    const char *pe = argc > 1 ? argv[1] : "artifacts/nfs.exe.unpacked";

    mem_init();
    mem_fault_handler_install();
    mem_altstack_install();
    if (mem_load_pe(pe)) return 1;
    overlay_clean_data(pe, argc, argv);

    mem_commit(STACK_LO, STACK_HI - STACK_LO);
    win32_init();
    setup_teb();
    if (getenv("PROSTREET_TLS_TEST") && !win32_tls_selftest()) return 1;
    dispatch_init();

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

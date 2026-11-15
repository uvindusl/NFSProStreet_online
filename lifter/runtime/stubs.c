/* Minimal link stubs for the Phase 3 compile gate.
 *
 * The lifted shards only need the CPU contract from cpu.h.  These definitions
 * let the whole generated set compile and link without pulling in the real
 * emulator (guest memory manager, dispatcher, Win32 surface).  They are
 * deliberately dumb: Phase 3 proves the C is well formed, not that it runs.
 */
#include "cpu.h"

uint8_t *g_mem;                                  /* guest memory base */
guest_fn_t g_dtab[DISPATCH_TEXT_SIZE];           /* direct dispatch cache */
uint8_t g_cbits[1u << 15];

void dispatch_indirect(struct CPU *c, uint32_t addr) {
    (void)c; (void)addr;
}
void dispatch_tail(struct CPU *c) {
    (void)c;
}
void unsupported_insn(CPU *c, uint32_t ip, const char *why) {
    (void)c; (void)ip; (void)why;
}
void cpuid_shim(CPU *c) {
    (void)c;
}
uint64_t rdtsc_shim(void) {
    return 0;
}
int split_render_limit(void) {
    return 0;
}
unsigned split_settings_address(unsigned original) {
    return original;
}

/* Phase 3 link gate: proves every lifted function in fntab.c resolves. */
#include <stdio.h>
#include "cpu.h"
#include "decls.h"

extern const uint32_t g_fn_count;

int main(void) {
    printf("lifted functions: %u\n", g_fn_count);
    return g_fn_count ? 0 : 1;
}

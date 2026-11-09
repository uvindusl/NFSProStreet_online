/* Wasm runtime heap: sbrk (set break) for the guest heap.
 *
 * The guest heap lives in the wasm linear memory above the guest image,
 * at addresses [HEAP_START, GUEST_END).  The host (page) gives the game
 * INITIAL_MB of this window; the game may ask for more through sbrk, and
 * the page may grow the wasm linear memory (it is growable) or OOM.
 *
 * The guest's VirtualAlloc / HeapCreate etc. go through heap_api() in
 * runtime/web/heap_web.c, which calls sbrk() below.
 */
#include "cpu.h"
#include <stdlib.h>
#include <string.h>

/* The wasm host provides the current break via this weak symbol,
 * initially set to HEAP_START. */
__attribute__((weak)) uint8_t *g_heap_brk = (uint8_t *)HEAP_START;

uint64_t heap_break(void)
{
    return (uint64_t)(uintptr_t)g_heap_brk;
}

/* sbrk: move the heap break by inc bytes (inc may be negative).
 * The host may also adjust g_heap_brk independently; this function
 * is the guest's view of the break.
 */
void *sbrk(intptr_t inc)
{
    if (inc == 0) return (void *)g_heap_brk;
    uintptr_t old = (uintptr_t)g_heap_brk;
    uintptr_t new = old + inc;
    if (new < (uintptr_t)HEAP_START) { errno = ENOMEM; return (void *)-1; }
    if (new > (uintptr_t)GUEST_END) { errno = ENOMEM; return (void *)-1; }
    g_heap_brk = (uint8_t *)new;
    return (void *)old;
}
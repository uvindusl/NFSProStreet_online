/* Wasm-specific platform glue that the lifted shards #include.
 * Included AFTER cpu.h (which defines g_mem, GUEST_END, MR* etc.).
 * Thewasm host sets g_mem = wasm_linear_memory before main().
 */

#ifndef NFSPS_WASM_PLAT_H
#define NFSPS_WASM_PLAT_H

/* The wasm host provides the linear memory base via this symbol.
 * The wasm-ld --global-base=GUEST_END places g_guest at address 0,
 * and the host initialises g_mem = (uint8_t *)g_guest before main.
 * Code keeps g_guest and K apart: data does g_guest + K, code does
 * just the constant K, so that clang does not turn constant-address
 * loads into g_guest + K lookups. */
extern __attribute__((weak)) uint8_t g_guest[];
#define GMEM_ g_guest

/* If a shard was compiled with NFSU2_GUEST_OBJ, it reaches guest memory
 * through g_guest: an undefined weak data symbol, which wasm-ld places at
 * address 0, so g_guest + a is the same address as g_mem + a.  The
 * difference is for clang's alias analysis.  g_mem + a is an inttoptr, which
 * may point anywhere, the restrict CPU struct included: every guest store
 * forced a reload of every register the function used next (c->esp after each
 * push) and every register write was stored at once.  g_guest is an object of
 * its own, and a global never aliases a restrict argument, so the registers
 * stay in wasm locals across guest loads and stores until the next call.
 * True because the CPU structs (and all of the runtime's data) live above
 * GUEST_END, where no guest access goes.  The runtime keeps g_mem: its own
 * accesses may well mix guest and host pointers.
 *
 * Code that does NOT have NFSU2_GUEST_OBJ (most shards) uses g_mem directly:
 *   MR32(a)  -> (*(gm_u32 *)(g_mem + (uint32_t)(a)))
 * and this works in wasm because g_mem + a is plain integer arithmetic into
 * the wasm linear memory. */

/* Wasm linear-memory size: the guest owns [0, GUEST_END). */
#ifndef GUEST_END
#define GUEST_END 0x28000000u   /* 640 MiB: the image, its heap and thread stacks */
#endif
#define GSZ ((uint64_t)GUEST_END)

/* The runtime heap grows upward from HEAP_START; the page gives INITIAL_MB. */
#define HEAP_START  (GUEST_END - 0x2000000u)   /* 2 GiB from the top = 2 GiB heap window */

/* --- sbrk: the wasm runtime heap (runtime/web/sbrk.c) ------------------ */
uint64_t heap_break(void);

/* --- page commitment (kept in sync with runtime/mem.c) ------------ */
/* In wasm mprotect is not needed – the linear memory is already RW.
 * The bitmap g_cbits tracks committed pages so mem_is_committed works. */
#define MEM_PAGE_SHIFT 14
#define MEM_PAGE_SIZE (1u << MEM_PAGE_SHIFT)

#endif
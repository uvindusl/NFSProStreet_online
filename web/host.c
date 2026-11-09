#include "cpu.h"
#include "decls.h"

#include <stdint.h>

#if defined(__wasm__) && __has_attribute(export_name)
#define WASM_EXPORT(name) __attribute__((export_name(name)))
#else
#define WASM_EXPORT(name)
#endif

guest_fn_t g_dtab[DISPATCH_TEXT_SIZE];
static uint32_t g_status;

extern const uint32_t g_fn_count;
extern const uint32_t g_fn_addr[];
extern const guest_fn_t g_fn_ptr[];

void dispatch_init(void)
{
    for (uint32_t i = 0; i < g_fn_count; ++i) {
        uint32_t offset = g_fn_addr[i] - DISPATCH_TEXT_BASE;
        if (offset < DISPATCH_TEXT_SIZE)
            g_dtab[offset] = g_fn_ptr[i];
    }
}

void dispatch_indirect(CPU *c, uint32_t address)
{
    (void)c;
    if (address >= UINT32_C(0xf0000000)) {
        g_status = 1;
        return;
    }
    uint32_t offset = address - DISPATCH_TEXT_BASE;
    if (offset < DISPATCH_TEXT_SIZE && g_dtab[offset])
        g_dtab[offset](c);
    else
        g_status = 2;
}

void dispatch_tail(CPU *c)
{
    dispatch_indirect(c, c->eip);
}

void unsupported_insn(CPU *c, uint32_t address, const char *mnemonic)
{
    (void)c;
    (void)address;
    (void)mnemonic;
    g_status = 3;
}

void cpuid_shim(CPU *c)
{
    c->eax = c->ebx = c->ecx = c->edx = 0;
}

uint64_t rdtsc_shim(void)
{
    return 0;
}

void import_init(void) {}

int import_call(CPU *c, uint32_t index)
{
    (void)c;
    (void)index;
    g_status = 1;
    return -1;
}

void guest_exit(int code)
{
    g_status = (uint32_t)(code ? code : 1);
}

uint64_t host_now_ns(void)
{
    return 0;
}

int host_trace_enabled(void)
{
    return 0;
}

WASM_EXPORT("prostreet_diagnostic")
uint32_t prostreet_diagnostic(void)
{
    dispatch_init();
    return g_fn_count;
}

WASM_EXPORT("prostreet_status")
uint32_t prostreet_status(void)
{
    return g_status;
}

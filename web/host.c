#include "cpu.h"
#include "decls.h"
#include "s2_import_dispatch.h"

#include <stdint.h>
#include <string.h>

#if defined(__wasm__) && __has_attribute(export_name)
#define WASM_EXPORT(name) __attribute__((export_name(name)))
#else
#define WASM_EXPORT(name)
#endif

guest_fn_t g_dtab[DISPATCH_TEXT_SIZE];
static uint32_t g_status;
static CPU g_cpu;
CPU *g_cur_cpu;

typedef struct {
    uint32_t iat;
    const char *dll;
    const char *name;
    int nargs;
} ImpEnt;

extern const uint32_t g_imp_count;
extern const ImpEnt g_imp[];

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

void import_init(void)
{
    for (uint32_t i = 0; i < g_imp_count; ++i)
        MR32(g_imp[i].iat) = S2_IMP_MAGIC | i;
}

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

WASM_EXPORT("prostreet_boot")
uint32_t prostreet_boot(void)
{
    if (MR16(UINT32_C(0x00400000)) != UINT16_C(0x5a4d)) {
        g_status = 4;
        return g_status;
    }
    memset(&g_cpu, 0, sizeof g_cpu);
    g_cpu.eip = UINT32_C(0x00828c25);
    g_cpu.esp = UINT32_C(0x01ff0000);
    g_cpu.fcw = UINT16_C(0x027f);
    g_cpu.top = 0;
    g_cur_cpu = &g_cpu;
    dispatch_init();
    import_init();
    guest_fn_t entry = dispatch_fast(g_cpu.eip);
    if (!entry) {
        g_status = 5;
        return g_status;
    }
    entry(&g_cpu);
    g_status = 6;
    return g_status;
}

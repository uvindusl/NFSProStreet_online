#define _POSIX_C_SOURCE 200809L

#include "cpu.h"
#include "s2_import_dispatch.h"

#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>
#include <string.h>
#include <time.h>

typedef struct {
    uint32_t iat;
    const char *dll;
    const char *name;
    int nargs;
} ImpEnt;

extern const ImpEnt g_imp[];
extern const uint32_t g_imp_count;
extern const uint32_t g_fn_addr[];
extern const guest_fn_t g_fn_ptr[];
extern const uint32_t g_fn_count;

guest_fn_t g_dtab[DISPATCH_TEXT_SIZE];
volatile int g_guest_exited;
int g_guest_exit_code;
jmp_buf g_boot_jmp;
int g_boot_jmp_active;

static S2ImportEntry *g_import_entries;
static uint32_t g_import_count;
static int g_import_ready;

static int guest_read32(void *opaque, uint32_t address, uint32_t *value)
{
    (void)opaque;
    if (!value || address > UINT32_MAX - 4u ||
        !mem_is_committed(address) || !mem_is_committed(address + 3u))
        return 0;
    *value = MR32(address);
    return 1;
}

static int guest_write32(uint32_t address, uint64_t value)
{
    if (address > UINT32_MAX - 4u ||
        !mem_is_committed(address) || !mem_is_committed(address + 3u))
        return 0;
    MR32(address) = (uint32_t)value;
    return 1;
}

static int guest_write_filetime(uint32_t address)
{
    time_t seconds = time(NULL);
    if (seconds < 0)
        return 0;
    uint64_t ticks = ((uint64_t)seconds + UINT64_C(11644473600)) *
                     UINT64_C(10000000);
    return guest_write32(address, ticks) &&
           guest_write32(address + 4u, ticks >> 32);
}

static int import_handler(void *opaque, const S2ImportEntry *entry,
                          const uint32_t *args, uint32_t argc,
                          uint32_t *eax, uint32_t *edx)
{
    CPU *cpu = opaque;
    (void)edx;
    if (strcmp(entry->dll, "KERNEL32.dll") == 0 &&
        strcmp(entry->name, "GetCurrentProcessId") == 0 && argc == 0) {
        *eax = 1;
        return 0;
    }
    if (strcmp(entry->dll, "KERNEL32.dll") == 0 &&
        strcmp(entry->name, "GetCurrentThreadId") == 0 && argc == 0) {
        *eax = 1;
        return 0;
    }
    if (strcmp(entry->dll, "KERNEL32.dll") == 0 &&
        strcmp(entry->name, "GetTickCount") == 0 && argc == 0) {
        *eax = (uint32_t)(host_now_ns() / UINT64_C(1000000));
        return 0;
    }
    if (strcmp(entry->dll, "KERNEL32.dll") == 0 &&
        strcmp(entry->name, "QueryPerformanceCounter") == 0 &&
        argc == 1 && guest_write32(args[0], host_now_ns())) {
        *eax = 1;
        return 0;
    }
    if (strcmp(entry->dll, "KERNEL32.dll") == 0 &&
        strcmp(entry->name, "GetSystemTimeAsFileTime") == 0 &&
        argc == 1 && guest_write_filetime(args[0])) {
        *eax = 1;
        return 0;
    }
    if (strcmp(entry->dll, "KERNEL32.dll") == 0 &&
        strcmp(entry->name, "Sleep") == 0 && argc == 1) {
        struct timespec delay = {
            .tv_sec = args[0] / 1000u,
            .tv_nsec = (long)(args[0] % 1000u) * 1000000L,
        };
        nanosleep(&delay, NULL);
        return 0;
    }
    if (strcmp(entry->dll, "KERNEL32.dll") == 0 &&
        strcmp(entry->name, "InterlockedCompareExchange") == 0 &&
        argc == 3 && guest_read32(cpu, args[0], (uint32_t *)eax)) {
        uint32_t old = *eax;
        if (old == args[2])
            guest_write32(args[0], args[1]);
        *eax = old;
        return 0;
    }
    if (strcmp(entry->dll, "KERNEL32.dll") == 0 &&
        strcmp(entry->name, "InterlockedExchange") == 0 &&
        argc == 2 && guest_read32(cpu, args[0], (uint32_t *)eax) &&
        guest_write32(args[0], args[1])) {
        return 0;
    }
    if (strcmp(entry->dll, "KERNEL32.dll") == 0 &&
        strcmp(entry->name, "InterlockedExchangeAdd") == 0 &&
        argc == 3 && guest_read32(cpu, args[0], (uint32_t *)eax) &&
        guest_write32(args[0], (uint32_t)(*eax + args[1]))) {
        return 0;
    }
    if (strcmp(entry->dll, "KERNEL32.dll") == 0 &&
        (strcmp(entry->name, "InterlockedIncrement") == 0 ||
         strcmp(entry->name, "InterlockedDecrement") == 0) &&
        argc == 1 && guest_read32(cpu, args[0], (uint32_t *)eax)) {
        uint32_t value = strcmp(entry->name, "InterlockedIncrement") == 0
                       ? *eax + 1u : *eax - 1u;
        if (guest_write32(args[0], value)) {
            *eax = value;
            return 0;
        }
    }
    if (strcmp(entry->dll, "MSVCR80.dll") == 0 &&
        (strcmp(entry->name, "_initterm_e") == 0 ||
         strcmp(entry->name, "_initterm") == 0)) {
        *eax = 0;
        return 0;
    }
    fprintf(stderr, "[boot] unsupported import %s!%s\n",
            entry->dll, entry->name);
    return -1;
}

static void import_log(void *opaque, const char *message)
{
    (void)opaque;
    fprintf(stderr, "%s\n", message);
}

static S2ImportConvention import_convention(const char *dll)
{
    if (!dll)
        return S2_IMPORT_CDECL;
    if (strcmp(dll, "MSVCR80.dll") == 0 ||
        strcmp(dll, "MSVCP80.dll") == 0)
        return S2_IMPORT_CDECL;
    return S2_IMPORT_STDCALL;
}

void import_init(void)
{
    if (g_import_ready)
        return;
    g_import_ready = 1;
    g_import_count = g_imp_count;
    g_import_entries = calloc(g_import_count, sizeof *g_import_entries);
    if (!g_import_entries) {
        fprintf(stderr, "[boot] unable to allocate import table\n");
        g_guest_exited = 1;
        g_guest_exit_code = 1;
        return;
    }
    for (uint32_t i = 0; i < g_import_count; ++i) {
        g_import_entries[i] = (S2ImportEntry){
            .dll = g_imp[i].dll,
            .name = g_imp[i].name,
            .arity = (uint32_t)(g_imp[i].nargs < 0 ? 0 : g_imp[i].nargs),
            .convention = import_convention(g_imp[i].dll),
        };
        if (mem_is_committed(g_imp[i].iat) &&
            mem_is_committed(g_imp[i].iat + 3u))
            MR32(g_imp[i].iat) = S2_IMP_MAGIC | i;
    }
}

int import_call(CPU *c, uint32_t index)
{
    if (!c || !g_import_ready || index >= g_import_count)
        return -1;
    S2ImportDispatch dispatch = {
        .entries = g_import_entries,
        .entry_count = g_import_count,
        .read32 = guest_read32,
        .handler = import_handler,
        .log = import_log,
        .max_arity = 64,
    };
    S2ImportCpu state = {.esp = c->esp, .eax = c->eax, .edx = c->edx};
    S2ImportStatus status = s2_import_dispatch(
        &dispatch, &state, S2_IMP_MAGIC | index);
    c->esp = state.esp;
    c->eax = state.eax;
    c->edx = state.edx;
    return status == S2_IMPORT_OK ? 0 : -1;
}

void dispatch_init(void)
{
    static int ready;
    if (ready)
        return;
    ready = 1;
    for (uint32_t i = 0; i < g_fn_count; ++i) {
        uint32_t address = g_fn_addr[i];
        uint32_t offset = address - DISPATCH_TEXT_BASE;
        if (offset < DISPATCH_TEXT_SIZE)
            g_dtab[offset] = g_fn_ptr[i];
    }
    fprintf(stderr, "[boot] registered %u lifted functions\n", g_fn_count);
}

void dispatch_indirect(CPU *c, uint32_t address)
{
    if (address >= S2_IMP_MAGIC) {
        if (import_call(c, address - S2_IMP_MAGIC) != 0) {
            g_guest_exited = 1;
            g_guest_exit_code = 2;
            if (g_boot_jmp_active)
                longjmp(g_boot_jmp, 1);
        }
        return;
    }
    uint32_t offset = address - DISPATCH_TEXT_BASE;
    if (offset < DISPATCH_TEXT_SIZE && g_dtab[offset]) {
        g_dtab[offset](c);
        return;
    }
    fprintf(stderr, "[boot] unresolved guest target 0x%08x\n", address);
    g_guest_exited = 1;
    g_guest_exit_code = 3;
    if (g_boot_jmp_active)
        longjmp(g_boot_jmp, 1);
}

void dispatch_tail(CPU *c)
{
    dispatch_indirect(c, c->eip);
}

void unsupported_insn(CPU *c, uint32_t address, const char *mnemonic)
{
    (void)c;
    fprintf(stderr, "[boot] unsupported instruction at 0x%08x: %s\n",
            address, mnemonic ? mnemonic : "?");
    g_guest_exited = 1;
    g_guest_exit_code = 4;
    if (g_boot_jmp_active)
        longjmp(g_boot_jmp, 1);
}

void cpuid_shim(CPU *c)
{
    c->eax = c->ebx = c->ecx = c->edx = 0;
}

uint64_t rdtsc_shim(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0;
    return (uint64_t)ts.tv_sec * UINT64_C(3000000000) +
           (uint64_t)ts.tv_nsec * UINT64_C(3);
}

void guest_exit(int code)
{
    g_guest_exited = 1;
    g_guest_exit_code = code;
}

uint64_t host_now_ns(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0;
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}

int host_trace_enabled(void)
{
    return 1;
}

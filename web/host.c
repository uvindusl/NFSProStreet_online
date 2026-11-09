#include "cpu.h"
#include "decls.h"
#include "host_abi.h"
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
static uint32_t g_heap = UINT32_C(0x01f70000);

static inline uint8_t *guest_ptr(uint32_t address)
{
    return (uint8_t *)(uintptr_t)address;
}

static uint32_t guest_string(uint32_t address, const char *text, uint32_t cap)
{
    uint32_t length = (uint32_t)strlen(text);
    if (length + 1u > cap)
        length = cap - 1u;
    memcpy(guest_ptr(address), text, length);
    guest_ptr(address)[length] = '\0';
    return length;
}

static uint32_t guest_alloc(uint32_t size)
{
    uint32_t address = (g_heap + 15u) & ~15u;
    uint32_t bytes = (size + 15u) & ~15u;
    g_heap = address + bytes;
    return address;
}

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
    if (!c || index >= g_imp_count)
        return -1;
    const ImpEnt *imp = &g_imp[index];
    uint32_t args[8] = {0};
    uint32_t argc = imp->nargs < 0 ? 0u : (uint32_t)imp->nargs;
    if (argc > 8)
        return -1;
    for (uint32_t i = 0; i < argc; ++i)
        args[i] = MR32(c->esp + 4u + i * 4u);

    uint32_t result = 0;
    if (strcmp(imp->dll, "KERNEL32.dll") == 0 &&
        strcmp(imp->name, "GetCurrentProcessId") == 0) {
        result = 1;
    } else if (strcmp(imp->dll, "KERNEL32.dll") == 0 &&
               strcmp(imp->name, "GetCurrentThreadId") == 0) {
        result = 1;
    } else if (strcmp(imp->dll, "KERNEL32.dll") == 0 &&
               strcmp(imp->name, "GetCurrentProcess") == 0) {
        result = UINT32_MAX;
    } else if (strcmp(imp->dll, "KERNEL32.dll") == 0 &&
               strcmp(imp->name, "GetCurrentThread") == 0) {
        result = UINT32_MAX - 1u;
    } else if (strcmp(imp->dll, "KERNEL32.dll") == 0 &&
               strcmp(imp->name, "GetModuleHandleA") == 0) {
        result = UINT32_C(0x00400000);
    } else if (strcmp(imp->dll, "KERNEL32.dll") == 0 &&
               strcmp(imp->name, "GetTickCount") == 0) {
        result = (uint32_t)nfsu2_now_ms();
    } else if (strcmp(imp->dll, "KERNEL32.dll") == 0 &&
               strcmp(imp->name, "Sleep") == 0) {
        result = 0;
    } else if (strcmp(imp->dll, "KERNEL32.dll") == 0 &&
               strcmp(imp->name, "QueryPerformanceCounter") == 0 &&
               argc == 1) {
        MR64(args[0]) = (uint64_t)(nfsu2_now_ms() * 1000000.0);
        result = 1;
    } else if (strcmp(imp->dll, "KERNEL32.dll") == 0 &&
               strcmp(imp->name, "QueryPerformanceFrequency") == 0 &&
               argc == 1) {
        MR64(args[0]) = UINT64_C(1000000000);
        result = 1;
    } else if (strcmp(imp->dll, "KERNEL32.dll") == 0 &&
               strcmp(imp->name, "GetLastError") == 0) {
        result = 0;
    } else if (strcmp(imp->dll, "KERNEL32.dll") == 0 &&
               strcmp(imp->name, "IsDebuggerPresent") == 0) {
        result = 0;
    } else if (strcmp(imp->dll, "KERNEL32.dll") == 0 &&
               strcmp(imp->name, "GetCommandLineA") == 0) {
        result = UINT32_C(0x01f6f000);
        guest_string(result, "nfsps.exe", 256);
    } else if (strcmp(imp->dll, "KERNEL32.dll") == 0 &&
               strcmp(imp->name, "GetStartupInfoA") == 0 && argc == 1) {
        memset(guest_ptr(args[0]), 0, 68);
        MR32(args[0]) = 68;
        result = 0;
    } else if (strcmp(imp->dll, "KERNEL32.dll") == 0 &&
               strcmp(imp->name, "GetVersionExA") == 0 && argc == 1) {
        uint32_t size = MR32(args[0]);
        memset(guest_ptr(args[0]), 0, size < 148u ? size : 148u);
        MR32(args[0]) = size;
        MR32(args[0] + 4u) = 1;
        MR32(args[0] + 8u) = 5;
        MR32(args[0] + 12u) = 0;
        result = 1;
    } else if (strcmp(imp->dll, "KERNEL32.dll") == 0 &&
               strcmp(imp->name, "GetSystemInfo") == 0 && argc == 1) {
        memset(guest_ptr(args[0]), 0, 36);
        MR32(args[0]) = 0;
        MR32(args[0] + 4u) = 0x1000;
        MR32(args[0] + 16u) = 1;
        result = 0;
    } else if (strcmp(imp->dll, "KERNEL32.dll") == 0 &&
               strcmp(imp->name, "GetDriveTypeA") == 0) {
        result = 3;
    } else if (strcmp(imp->dll, "KERNEL32.dll") == 0 &&
               strcmp(imp->name, "GetLogicalDrives") == 0) {
        result = 1;
    } else if (strcmp(imp->dll, "KERNEL32.dll") == 0 &&
               strcmp(imp->name, "GetCurrentDirectoryA") == 0 && argc == 2) {
        result = guest_string(args[1], "C:\\", args[0]);
    } else if (strcmp(imp->dll, "KERNEL32.dll") == 0 &&
               strcmp(imp->name, "GetSystemDirectoryA") == 0 && argc == 2) {
        result = guest_string(args[1], "C:\\Windows", args[0]);
    } else if (strcmp(imp->dll, "KERNEL32.dll") == 0 &&
               strcmp(imp->name, "VirtualAlloc") == 0 && argc == 4) {
        result = guest_alloc(args[1]);
    } else if (strcmp(imp->dll, "KERNEL32.dll") == 0 &&
               strcmp(imp->name, "VirtualProtect") == 0 && argc == 4) {
        if (args[3])
            MR32(args[3]) = 0x40;
        result = 1;
    } else if (strcmp(imp->dll, "KERNEL32.dll") == 0 &&
               strcmp(imp->name, "HeapCreate") == 0) {
        result = 1;
    } else if (strcmp(imp->dll, "KERNEL32.dll") == 0 &&
               strcmp(imp->name, "HeapAlloc") == 0 && argc == 3) {
        result = guest_alloc(args[2]);
    } else if (strcmp(imp->dll, "KERNEL32.dll") == 0 &&
               (strcmp(imp->name, "HeapFree") == 0 ||
                strcmp(imp->name, "HeapDestroy") == 0)) {
        result = 1;
    } else if (strcmp(imp->dll, "KERNEL32.dll") == 0 &&
               strcmp(imp->name, "TlsAlloc") == 0) {
        result = 0;
    } else if (strcmp(imp->dll, "KERNEL32.dll") == 0 &&
               strcmp(imp->name, "TlsGetValue") == 0) {
        result = 0;
    } else if (strcmp(imp->dll, "KERNEL32.dll") == 0 &&
               strcmp(imp->name, "TlsSetValue") == 0) {
        result = 1;
    } else if (strcmp(imp->dll, "KERNEL32.dll") == 0 &&
               (strcmp(imp->name, "InitializeCriticalSection") == 0 ||
                strcmp(imp->name, "DeleteCriticalSection") == 0 ||
                strcmp(imp->name, "EnterCriticalSection") == 0 ||
                strcmp(imp->name, "LeaveCriticalSection") == 0)) {
        result = 0;
    } else if (strcmp(imp->dll, "KERNEL32.dll") == 0 &&
               (strcmp(imp->name, "CreateEventA") == 0 ||
                strcmp(imp->name, "CreateMutexA") == 0 ||
                strcmp(imp->name, "CreateSemaphoreA") == 0 ||
                strcmp(imp->name, "CreateThread") == 0)) {
        result = 1;
    } else if (strcmp(imp->dll, "KERNEL32.dll") == 0 &&
               (strcmp(imp->name, "WaitForSingleObject") == 0 ||
                strcmp(imp->name, "WaitForMultipleObjects") == 0)) {
        result = 0;
    } else if (strcmp(imp->dll, "KERNEL32.dll") == 0 &&
               (strcmp(imp->name, "SetEvent") == 0 ||
                strcmp(imp->name, "ResetEvent") == 0 ||
                strcmp(imp->name, "ReleaseMutex") == 0 ||
                strcmp(imp->name, "ReleaseSemaphore") == 0 ||
                strcmp(imp->name, "CloseHandle") == 0)) {
        result = 1;
    } else if (strcmp(imp->dll, "KERNEL32.dll") == 0 &&
               strcmp(imp->name, "MultiByteToWideChar") == 0 &&
               argc == 6) {
        result = 0;
    } else if (strcmp(imp->dll, "KERNEL32.dll") == 0 &&
               strcmp(imp->name, "WideCharToMultiByte") == 0 &&
               argc == 8) {
        result = 0;
    } else if (strcmp(imp->dll, "KERNEL32.dll") == 0 &&
               strcmp(imp->name, "InterlockedCompareExchange") == 0 &&
               argc == 3) {
        result = MR32(args[0]);
        if (result == args[2])
            MR32(args[0]) = args[1];
    } else if (strcmp(imp->dll, "KERNEL32.dll") == 0 &&
               strcmp(imp->name, "InterlockedExchange") == 0 &&
               argc == 2) {
        result = MR32(args[0]);
        MR32(args[0]) = args[1];
    } else if (strcmp(imp->dll, "KERNEL32.dll") == 0 &&
               strcmp(imp->name, "InterlockedExchangeAdd") == 0 &&
               argc == 2) {
        result = MR32(args[0]);
        MR32(args[0]) += args[1];
    } else if (strcmp(imp->dll, "KERNEL32.dll") == 0 &&
               strcmp(imp->name, "InterlockedIncrement") == 0 &&
               argc == 1) {
        result = ++MR32(args[0]);
    } else if (strcmp(imp->dll, "KERNEL32.dll") == 0 &&
               strcmp(imp->name, "InterlockedDecrement") == 0 &&
               argc == 1) {
        result = --MR32(args[0]);
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               (strcmp(imp->name, "_initterm") == 0 ||
                strcmp(imp->name, "_initterm_e") == 0)) {
        result = 0;
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               (strcmp(imp->name, "malloc") == 0 ||
                strcmp(imp->name, "_aligned_malloc") == 0) &&
               argc == 1) {
        result = g_heap;
        g_heap += (args[0] + 15u) & ~15u;
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               strcmp(imp->name, "calloc") == 0 && argc == 2) {
        result = g_heap;
        uint32_t bytes = args[0] * args[1];
        memset(guest_ptr(g_heap), 0, bytes);
        g_heap += (bytes + 15u) & ~15u;
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               (strcmp(imp->name, "free") == 0 ||
                strcmp(imp->name, "_aligned_free") == 0)) {
        result = 0;
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               strcmp(imp->name, "memset") == 0 && argc == 3) {
        memset(guest_ptr(args[0]), args[1], args[2]);
        result = args[0];
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               strcmp(imp->name, "memcpy") == 0 && argc == 3) {
        memcpy(guest_ptr(args[0]), guest_ptr(args[1]), args[2]);
        result = args[0];
    } else {
        g_status = 1;
        return -1;
    }

    c->eax = result;
    if (s2_import_convention(imp->dll, imp->name) == S2_IMPORT_STDCALL)
        c->esp += argc * 4u;
    return 0;
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

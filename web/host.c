#include "cpu.h"
#include "decls.h"
#include "host_abi.h"
#include "s2_import_dispatch.h"

#include <stdint.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#if defined(__wasm__) && __has_attribute(export_name)
#define WASM_EXPORT(name) __attribute__((export_name(name)))
#else
#define WASM_EXPORT(name)
#endif

guest_fn_t g_dtab[DISPATCH_TEXT_SIZE];
static uint32_t g_status;
static CPU g_cpu;
CPU *g_cur_cpu;

/* Guest heap.  The image is mapped at 0x00400000 and (with the DRM sections
 * skipped by the page) ends below 0x01100000, so 0x02000000 leaves the whole
 * lower arena free up to GUEST_END.  VirtualAlloc/HeapAlloc/malloc share this
 * bump allocator until the guest heap layer replaces it. */
static uint32_t g_heap = UINT32_C(0x02000000);

/* Diagnostics the page reads back after prostreet_boot(): the first import
 * with no shim yet, and a per-import call histogram.  "Runs further" is
 * measured by these, not by a feeling (AGENTS.md). */
static char g_last_import[192] = "none";
static uint32_t g_unknown_imports;
static uint32_t g_trace_imports;
extern const uint32_t g_imp_count;
static uint32_t g_imp_hits[512];

static inline uint8_t *guest_ptr(uint32_t address)
{
    return (uint8_t *)(uintptr_t)address;
}

static int guest_address_valid(uint32_t address)
{
    return address != 0 && address < GUEST_END;
}

static void fill_process_entry(uint32_t address)
{
    if (!address)
        return;
    memset(guest_ptr(address), 0, 296u);
    MR32(address + 0u) = 296u;
    MR32(address + 8u) = 1u;
    memcpy(guest_ptr(address + 32u), "nfsps.exe", sizeof "nfsps.exe");
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

/* ---- MSVCR80 helpers ----
 * Guest pointers are wasm linear-memory offsets, so the host libc can read
 * and write them directly.  Only the x86 calling convention has to be
 * bridged: the caller's varargs are 32-bit words at c->esp + 8. */
static uint32_t guest_arg(uint32_t argv, int index)
{
    return MR32(argv + (uint32_t)index * 4u);
}

/* Format into a host buffer.  argv is the guest address of the first vararg
 * (the x86 stack), so a va_list can be passed straight in: on i386 it is a
 * pointer to the same words.  Supports the conversions the game's log and
 * path code use; unknown conversions are copied verbatim. */
static int guest_vformat(char *dst, uint32_t cap, const char *fmt,
                         uint32_t argv, int argc)
{
    uint32_t n = 0;
    int ai = 0;
    for (const char *p = fmt; *p && n + 1u < cap; p++) {
        if (*p != '%') { dst[n++] = *p; continue; }
        p++;
        int width = 0, zero = 0;
        if (*p == '0') { zero = 1; p++; }
        while (*p >= '0' && *p <= '9') width = width * 10 + (*p++ - '0');
        if (*p == 'l' || *p == 'h') p++;
        char tmp[96];
        int len = 0;
        switch (*p) {
        case '%': dst[n++] = '%'; continue;
        case 'c': tmp[0] = (char)(ai < argc ? guest_arg(argv, ai++) : 0); len = 1; break;
        case 's': {
            uint32_t address = ai < argc ? guest_arg(argv, ai++) : 0;
            const char *s = address ? guest_ptr(address) : "(null)";
            for (len = 0; s[len] && len < (int)sizeof tmp - 1; len++) tmp[len] = s[len];
            break;
        }
        case 'd': case 'i':
            len = snprintf(tmp, sizeof tmp, "%d",
                           ai < argc ? (int32_t)guest_arg(argv, ai++) : 0);
            break;
        case 'u':
            len = snprintf(tmp, sizeof tmp, "%u", ai < argc ? guest_arg(argv, ai++) : 0);
            break;
        case 'x': case 'X':
            len = snprintf(tmp, sizeof tmp, *p == 'x' ? "%x" : "%X",
                           ai < argc ? guest_arg(argv, ai++) : 0);
            break;
        case 'p':
            len = snprintf(tmp, sizeof tmp, "0x%08x",
                           ai < argc ? guest_arg(argv, ai++) : 0);
            break;
        default:
            tmp[0] = '%'; tmp[1] = *p ? *p : '%'; len = 2;
            break;
        }
        if (*p == 0) break;
        while (width > len && n + 1u < cap) { dst[n++] = zero ? '0' : ' '; width--; }
        for (int i = 0; i < len && n + 1u < cap; i++) dst[n++] = tmp[i];
    }
    dst[n] = 0;
    return (int)n;
}

/* MSVC struct tm: nine ints, no tm_gmtoff.  POSIX struct tm has extra
 * members, so convert field by field instead of copying. */
static void put_msvc_tm(uint32_t dst, const struct tm *t)
{
    MR32(dst + 0u)  = (uint32_t)t->tm_sec;
    MR32(dst + 4u)  = (uint32_t)t->tm_min;
    MR32(dst + 8u)  = (uint32_t)t->tm_hour;
    MR32(dst + 12u) = (uint32_t)t->tm_mday;
    MR32(dst + 16u) = (uint32_t)t->tm_mon;
    MR32(dst + 20u) = (uint32_t)t->tm_year;
    MR32(dst + 24u) = (uint32_t)t->tm_wday;
    MR32(dst + 28u) = (uint32_t)t->tm_yday;
    MR32(dst + 32u) = (uint32_t)t->tm_isdst;
}

static int64_t guest_time_now(void)
{
    /* performance.now() has an arbitrary origin; only differences and the
     * "some time in 2007" property matter before the file layer is in. */
    return (int64_t)(nfsu2_now_ms() / 1000.0);
}

/* ---- Win32 string conversion (bring-up: CP1252/UTF-8 as Latin-1) ----
 * Guest wide strings are UTF-16LE in guest memory.  Until the locale layer
 * exists, every BMP code unit <= 0xFF maps one byte, anything else '?', which
 * is what the CRT does for CP_ACP on the characters the game uses in paths. */
static int guest_wcs_to_mb(uint32_t src, int src_len, uint32_t dst, int dst_len)
{
    int n = 0;
    for (; src_len < 0 || n < src_len; n++) {
        uint16_t w = MR16(src + (uint32_t)n * 2u);
        if (!w)
            break;
        if (dst_len > 0) {
            if (n >= dst_len)
                break;
            MR8(dst + (uint32_t)n) = w <= 0xFFu ? (uint8_t)w : (uint8_t)'?';
        }
    }
    return n;
}

static int guest_mb_to_wcs(uint32_t src, int src_len, uint32_t dst, int dst_len)
{
    int n = 0;
    for (; src_len < 0 || n < src_len; n++) {
        uint8_t b = MR8(src + (uint32_t)n);
        if (!b)
            break;
        if (dst_len > 0) {
            if (n >= dst_len)
                break;
            MR16(dst + (uint32_t)n * 2u) = b;
        }
    }
    return n;
}

static uint32_t guest_alloc(uint32_t size)
{
    uint32_t address = (g_heap + 15u) & ~15u;
    uint32_t bytes = (size + 15u) & ~15u;
    /* The bump allocator must stay inside the guest arena.  Returning 0 is
     * what VirtualAlloc/HeapAlloc report on failure; a wrap past 4 GiB would
     * hand the guest a pointer into the runtime's own data. */
    if (address < g_heap || address + bytes < address ||
        address + bytes > GUEST_END)
        return 0;
    g_heap = address + bytes;
    memset(guest_ptr(address), 0, bytes);   /* VirtualAlloc zeroes, malloc may not */
    return address;
}

/* runtime/web/com.c allocates its COM objects and lock buffers with the same
 * guest bump allocator, so they stay inside GUEST_END. */
uint32_t host_guest_alloc(uint32_t size)
{
    return guest_alloc(size);
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
int com_call(CPU *c, uint32_t magic);
uint32_t com_direct3dcreate9(void);

void dispatch_init(void)
{
    for (uint32_t i = 0; i < g_fn_count; ++i) {
        uint32_t offset = g_fn_addr[i] - DISPATCH_TEXT_BASE;
        if (offset < DISPATCH_TEXT_SIZE)
            g_dtab[offset] = g_fn_ptr[i];
    }
}

/* Defined below; dispatch_indirect needs it for IAT slots. */
int import_call(CPU *c, uint32_t index);

void dispatch_indirect(CPU *c, uint32_t address)
{
    if (address >= UINT32_C(0xe0000000) &&
        address < UINT32_C(0xf0000000)) {
        if (com_call(c, address) != 0)
            g_status = 1;
        return;
    }
    /* IAT slot: 0xF0000000 | index (docs/S2_BRINGUP.md).  The lifted call
     * site either already cleaned the stack (cdecl) or import_call pops the
     * stdcall arguments; either way the call returns like a guest function. */
    if (address >= UINT32_C(0xf0000000)) {
        if (import_call(c, address & UINT32_C(0x0fffffff)) != 0)
            g_status = 1;
        return;
    }
    uint32_t offset = address - DISPATCH_TEXT_BASE;
    if (offset < DISPATCH_TEXT_SIZE && g_dtab[offset])
        g_dtab[offset](c);
    else {
        snprintf(g_last_import, sizeof g_last_import,
                 "no dispatch for %08x (eip %08x)", address, c->eip);
        g_status = 2;
        /* A call/jmp to an address with no lifted body is a bad function
         * pointer, not something to continue through: trap here so the stack
         * shows the caller instead of a later, unrelated fault. */
        __builtin_trap();
    }
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

/* Some imports are variables, not functions: the loader fills the IAT slot
 * with the address of the variable, and the game reads/writes through it.
 * Treating one as a call (or as a magic IAT slot) faults.
 *
 * __initenv/_environ are `char **`.  _adjust_fdiv is the CRT's int flag:
 * __cinit does `mov eax,[iat]; mov eax,[eax]`, so the slot must hold a
 * pointer to a guest cell (a magic slot faults at 0xF0000000). */
static int is_data_import(const char *dll, const char *name)
{
    return strcmp(dll, "MSVCR80.dll") == 0 &&
           (strcmp(name, "__initenv") == 0 ||
            strcmp(name, "_environ") == 0 ||
            strcmp(name, "_adjust_fdiv") == 0);
}

void import_init(void)
{
    /* Scratch cells for data imports, just below the main guest stack. */
    uint32_t data_cell = UINT32_C(0x01100000);
    for (uint32_t i = 0; i < g_imp_count; ++i) {
        if (is_data_import(g_imp[i].dll, g_imp[i].name)) {
            MR32(data_cell) = 0;
            MR32(g_imp[i].iat) = data_cell;
            data_cell += 16;
        } else {
            MR32(g_imp[i].iat) = S2_IMP_MAGIC | i;
        }
    }
}

int import_call(CPU *c, uint32_t index)
{
    if (!c || index >= g_imp_count)
        return -1;
    const ImpEnt *imp = &g_imp[index];
    if (g_trace_imports)
        fprintf(stderr, "[imp] %s!%s argc=%d esp=%08x\n",
                imp->dll, imp->name, imp->nargs, c->esp);
    if (index < sizeof g_imp_hits / sizeof g_imp_hits[0])
        g_imp_hits[index]++;
    uint32_t args[16] = {0};
    uint32_t argc = imp->nargs < 0 ? 0u : (uint32_t)imp->nargs;
    if (argc > 16)
        return -1;
    for (uint32_t i = 0; i < argc; ++i)
        args[i] = MR32(c->esp + 4u + i * 4u);

    uint32_t result = 0;
    uint32_t result_hi = 0;
    int set_edx = 0;      /* only 64-bit returns write edx:eax */
    /* MSVCR80 imports are cdecl and several have no arity in the generated
     * table, so read their arguments straight off the x86 stack. */
#define A(i) (MR32(c->esp + 4u + (uint32_t)(i) * 4u))
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
        result = UINT32_C(0x01110000);
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
                strcmp(imp->name, "CloseHandle") == 0 ||
                strcmp(imp->name, "DuplicateHandle") == 0 ||
                strcmp(imp->name, "CreateFileA") == 0 ||
                strcmp(imp->name, "ReadFile") == 0)) {
        result = 1;
    } else if (strcmp(imp->dll, "KERNEL32.dll") == 0 &&
                strcmp(imp->name, "CreateToolhelp32Snapshot") == 0) {
        result = 1;
    } else if (strcmp(imp->dll, "KERNEL32.dll") == 0 &&
                strcmp(imp->name, "Process32First") == 0) {
        if (g_trace_imports)
            fprintf(stderr, "[Process32First] snapshot=%08x entry=%08x\n",
                     args[0], args[1]);
        fill_process_entry(args[1]);
        result = 1;
    } else if (strcmp(imp->dll, "KERNEL32.dll") == 0 &&
                strcmp(imp->name, "Process32Next") == 0) {
        result = 0;
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
        /* The CRT's static initialisers: an array of function pointers in
         * [A(0), A(1)).  They must actually run, or C++ globals and the
         * game's own startup registration never happen. */
        uint32_t begin = A(0), end = A(1);
        result = 0;
        if (g_trace_imports)
            fprintf(stderr, "[initterm] %08x..%08x\n", begin, end);
        for (uint32_t p = begin; p + 4u <= end; p += 4u) {
            uint32_t fnaddr = MR32(p);
            if (g_trace_imports)
                fprintf(stderr, "[initterm]  [%08x] = %08x\n", p, fnaddr);
            if (fnaddr)
                DISPATCH_CALL(c, fnaddr);
        }
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               (strcmp(imp->name, "malloc") == 0 ||
                strcmp(imp->name, "_aligned_malloc") == 0) &&
               argc == 1) {
        result = guest_alloc(args[0]);
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               strcmp(imp->name, "calloc") == 0 && argc == 2) {
        uint64_t bytes = (uint64_t)args[0] * args[1];
        result = bytes > UINT32_MAX ? 0 : guest_alloc((uint32_t)bytes);
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               (strcmp(imp->name, "free") == 0 ||
                strcmp(imp->name, "_aligned_free") == 0)) {
        result = 0;
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               strcmp(imp->name, "memset") == 0) {
        /* memset always takes 3 args (dst, src, count); the import table
         * arity may be 0 because the PE import directory doesn't encode
         * calling convention, so we read the args directly from the stack. */
        memset(guest_ptr(A(0)), A(1), A(2));
        result = A(0);
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               strcmp(imp->name, "memcpy") == 0 && argc == 3) {
        memcpy(guest_ptr(args[0]), guest_ptr(args[1]), args[2]);
        result = args[0];
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               (strcmp(imp->name, "_control87") == 0 ||
                strcmp(imp->name, "_controlfp_s") == 0 ||
                strcmp(imp->name, "__setusermatherr") == 0 ||
                strcmp(imp->name, "_configthreadlocale") == 0 ||
                strcmp(imp->name, "_setjmp3") == 0 ||
                strcmp(imp->name, "_lock") == 0 ||
                strcmp(imp->name, "_unlock") == 0)) {
        result = 0;
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               (strcmp(imp->name, "__p__fmode") == 0 ||
                strcmp(imp->name, "__p__commode") == 0)) {
        /* int *__p__fmode(void): distinct CRT globals, both default 0. */
        result = strcmp(imp->name, "__p__fmode") == 0
                 ? UINT32_C(0x01120000) : UINT32_C(0x01120004);
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               (strcmp(imp->name, "_amsg_exit") == 0 ||
                strcmp(imp->name, "_invalid_parameter_noinfo") == 0 ||
                strcmp(imp->name, "_purecall") == 0 ||
                strcmp(imp->name, "_crt_debugger_hook") == 0 ||
                strcmp(imp->name, "_invoke_watson") == 0 ||
                strcmp(imp->name, "_cexit") == 0)) {
        result = 0;
    } else if (strcmp(imp->dll, "WINMM.dll") == 0 &&
               strcmp(imp->name, "timeGetTime") == 0) {
        result = (uint32_t)nfsu2_now_ms();
    } else if (strcmp(imp->dll, "WINMM.dll") == 0 &&
               (strcmp(imp->name, "timeBeginPeriod") == 0 ||
                strcmp(imp->name, "timeEndPeriod") == 0)) {
        result = 0;
    } else if (strcmp(imp->dll, "USER32.dll") == 0 &&
               (strcmp(imp->name, "ShowCursor") == 0 ||
                strcmp(imp->name, "ShowWindow") == 0 ||
                strcmp(imp->name, "UpdateWindow") == 0 ||
                strcmp(imp->name, "SetCursorPos") == 0 ||
                strcmp(imp->name, "GetCursorPos") == 0 ||
                strcmp(imp->name, "GetClientRect") == 0)) {
        result = 1;
    } else if (strcmp(imp->dll, "USER32.dll") == 0 &&
               (strcmp(imp->name, "RegisterClassA") == 0 ||
                strcmp(imp->name, "RegisterClassExA") == 0 ||
                strcmp(imp->name, "UnregisterClassA") == 0 ||
                strcmp(imp->name, "DestroyWindow") == 0 ||
                strcmp(imp->name, "ReleaseDC") == 0)) {
        result = 1;
    } else if (strcmp(imp->dll, "USER32.dll") == 0 &&
               (strcmp(imp->name, "GetDC") == 0 ||
                strcmp(imp->name, "CreateWindowExA") == 0)) {
        result = 1;
    } else if (strcmp(imp->dll, "USER32.dll") == 0 &&
               (strcmp(imp->name, "PeekMessageA") == 0 ||
                strcmp(imp->name, "GetMessageA") == 0 ||
                strcmp(imp->name, "TranslateMessage") == 0 ||
                strcmp(imp->name, "DispatchMessageA") == 0)) {
        result = 0;
    } else if (strcmp(imp->dll, "KERNEL32.dll") == 0 &&
               (strcmp(imp->name, "LoadLibraryA") == 0 ||
                strcmp(imp->name, "LoadLibraryW") == 0)) {
        result = 1;
    } else if (strcmp(imp->dll, "KERNEL32.dll") == 0 &&
               strcmp(imp->name, "GetProcAddress") == 0) {
        result = 0;
    } else if (strcmp(imp->dll, "KERNEL32.dll") == 0 &&
               strcmp(imp->name, "GetSystemTimeAsFileTime") == 0 && argc == 1) {
        /* 100 ns ticks since 1601-01-01; the runtime clock is monotonic in
         * the browser, which is all the CRT's seed and timing use needs. */
        MR64(args[0]) = UINT64_C(116444736000000000) +
                        (uint64_t)(nfsu2_now_ms() * 10000.0);
        result = 0;
    } else if (strcmp(imp->dll, "KERNEL32.dll") == 0 &&
               (strcmp(imp->name, "GetSystemTime") == 0 ||
                strcmp(imp->name, "GetLocalTime") == 0) && argc == 1) {
        memset(guest_ptr(args[0]), 0, 16);
        MR16(args[0] + 0u) = 2007;   /* the release year, a stable answer */
        MR16(args[0] + 2u) = 11;
        MR16(args[0] + 6u) = 10;
        result = 0;
    } else if (strcmp(imp->dll, "KERNEL32.dll") == 0 &&
               strcmp(imp->name, "GetSystemTimeAdjustment") == 0 && argc == 3) {
        if (args[0]) MR32(args[0]) = 0;
        if (args[1]) MR32(args[1]) = 0;
        if (args[2]) MR32(args[2]) = 0;
        result = 1;
    } else if (strcmp(imp->dll, "KERNEL32.dll") == 0 &&
               strcmp(imp->name, "IsProcessorFeaturePresent") == 0) {
        result = 1;   /* SSE present */
    } else if (strcmp(imp->dll, "KERNEL32.dll") == 0 &&
               strcmp(imp->name, "GetACP") == 0) {
        result = 1252;
    } else if (strcmp(imp->dll, "KERNEL32.dll") == 0 &&
               strcmp(imp->name, "InitializeCriticalSectionAndSpinCount") == 0) {
        if (argc >= 1) MR32(args[0]) = 0;
        result = 1;
    } else if (strcmp(imp->dll, "KERNEL32.dll") == 0 &&
               (strcmp(imp->name, "GetModuleHandleExA") == 0 ||
                strcmp(imp->name, "GetModuleHandleExW") == 0)) {
        if (argc >= 3 && args[2]) MR32(args[2]) = UINT32_C(0x00400000);
        result = 1;
    } else if (strcmp(imp->dll, "KERNEL32.dll") == 0 &&
               strcmp(imp->name, "GetModuleFileNameA") == 0 && argc == 3) {
        result = guest_string(args[1], "nfsps.exe", args[2]);
    } else if (strcmp(imp->dll, "KERNEL32.dll") == 0 &&
               strcmp(imp->name, "GetModuleFileNameW") == 0 && argc == 3) {
        const char *name = "nfsps.exe";
        uint32_t n = 0;
        while (name[n] && n + 1u < args[2]) {
            MR16(args[1] + n * 2u) = (uint8_t)name[n];
            n++;
        }
        if (args[2]) MR16(args[1] + n * 2u) = 0;
        result = n;
    } else if (strcmp(imp->dll, "KERNEL32.dll") == 0 &&
               strcmp(imp->name, "GetProcessHeap") == 0) {
        result = 1;
    } else if (strcmp(imp->dll, "KERNEL32.dll") == 0 &&
               (strcmp(imp->name, "SetUnhandledExceptionFilter") == 0 ||
                strcmp(imp->name, "UnhandledExceptionFilter") == 0 ||
                strcmp(imp->name, "SetErrorMode") == 0)) {
        result = 0;
    } else if (strcmp(imp->dll, "KERNEL32.dll") == 0 &&
               strcmp(imp->name, "WideCharToMultiByte") == 0 && argc == 8) {
        result = (uint32_t)guest_wcs_to_mb(args[2], (int)args[3],
                                           args[4], (int)args[5]);
    } else if (strcmp(imp->dll, "KERNEL32.dll") == 0 &&
               strcmp(imp->name, "MultiByteToWideChar") == 0 && argc == 6) {
        result = (uint32_t)guest_mb_to_wcs(args[2], (int)args[3],
                                           args[4], (int)args[5]);
    } else if (strcmp(imp->dll, "KERNEL32.dll") == 0 &&
               strcmp(imp->name, "ExitProcess") == 0) {
        result = 0;
    } else if (strcmp(imp->dll, "KERNEL32.dll") == 0 &&
               strcmp(imp->name, "TerminateProcess") == 0) {
        result = 0;
    /* ---- MSVCR80 CRT: strings and memory ---- */
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               strcmp(imp->name, "strlen") == 0) {
        result = (uint32_t)strlen(guest_ptr(A(0)));
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               (strcmp(imp->name, "strcmp") == 0 ||
                strcmp(imp->name, "strcoll") == 0)) {
        result = (uint32_t)(int32_t)strcmp(guest_ptr(A(0)), guest_ptr(A(1)));
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               strcmp(imp->name, "strncmp") == 0) {
        result = (uint32_t)(int32_t)strncmp(guest_ptr(A(0)),
                                            guest_ptr(A(1)), A(2));
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               strcmp(imp->name, "_stricmp") == 0) {
        if (g_trace_imports)
            fprintf(stderr, "[_stricmp] %08x %08x\n", A(0), A(1));
        if (!guest_address_valid(A(0)) || !guest_address_valid(A(1))) {
            result = 0;
            goto import_done;
        }
        result = (uint32_t)(int32_t)strcasecmp(guest_ptr(A(0)),
                                               guest_ptr(A(1)));
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               strcmp(imp->name, "_strnicmp") == 0) {
        result = (uint32_t)(int32_t)strncasecmp(guest_ptr(A(0)),
                                                guest_ptr(A(1)), A(2));
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               strcmp(imp->name, "strcpy") == 0) {
        strcpy(guest_ptr(A(0)), guest_ptr(A(1)));
        result = A(0);
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               strcmp(imp->name, "strncpy") == 0) {
        strncpy(guest_ptr(A(0)), guest_ptr(A(1)), A(2));
        result = A(0);
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               strcmp(imp->name, "strcat") == 0) {
        strcat(guest_ptr(A(0)), guest_ptr(A(1)));
        result = A(0);
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               strcmp(imp->name, "strncat") == 0) {
        strncat(guest_ptr(A(0)), guest_ptr(A(1)), A(2));
        result = A(0);
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               strcmp(imp->name, "strchr") == 0) {
        result = (uint32_t)(uintptr_t)strchr(guest_ptr(A(0)), (int)A(1));
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               strcmp(imp->name, "strrchr") == 0) {
        if (g_trace_imports)
            fprintf(stderr, "[strrchr] %08x %08x\n", A(0), A(1));
        result = (uint32_t)(uintptr_t)strrchr(guest_ptr(A(0)), (int)A(1));
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               strcmp(imp->name, "strstr") == 0) {
        result = (uint32_t)(uintptr_t)strstr(guest_ptr(A(0)),
                                             guest_ptr(A(1)));
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               strcmp(imp->name, "strcspn") == 0) {
        result = (uint32_t)strcspn(guest_ptr(A(0)), guest_ptr(A(1)));
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               strcmp(imp->name, "strtok") == 0) {
        result = (uint32_t)(uintptr_t)strtok(guest_ptr(A(0)),
                                             guest_ptr(A(1)));
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               strcmp(imp->name, "memmove") == 0) {
        memmove(guest_ptr(A(0)), guest_ptr(A(1)), A(2));
        result = A(0);
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               strcmp(imp->name, "memcmp") == 0) {
        result = (uint32_t)(int32_t)memcmp(guest_ptr(A(0)),
                                           guest_ptr(A(1)), A(2));
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               strcmp(imp->name, "memmove_s") == 0) {
        memmove(guest_ptr(A(0)), guest_ptr(A(2)), A(3));
        result = 0;
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               strcmp(imp->name, "memcpy_s") == 0) {
        if (A(0) && A(2) && A(3))
            memcpy(guest_ptr(A(0)), guest_ptr(A(2)), A(3));
        result = 0;
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               strcmp(imp->name, "strcpy_s") == 0) {
        strcpy(guest_ptr(A(0)), guest_ptr(A(2)));
        result = 0;
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               strcmp(imp->name, "strncpy_s") == 0) {
        strncpy(guest_ptr(A(0)), guest_ptr(A(2)), A(3));
        guest_ptr(A(0))[A(1) ? A(1) - 1u : 0] = '\0';
        result = 0;
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               strcmp(imp->name, "realloc") == 0) {
        uint32_t bytes = A(1);
        uint32_t copy = guest_alloc(bytes);
        if (copy && A(0) && bytes)
            memcpy(guest_ptr(copy), guest_ptr(A(0)), bytes);
        result = copy;
    /* ---- MSVCR80 CRT: conversions ---- */
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               strcmp(imp->name, "atoi") == 0) {
        result = (uint32_t)atoi(guest_ptr(A(0)));
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               strcmp(imp->name, "strtol") == 0) {
        result = (uint32_t)strtol(guest_ptr(A(0)), (char **)guest_ptr(A(1)),
                                  (int)A(2));
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               strcmp(imp->name, "strtoul") == 0) {
        result = (uint32_t)strtoul(guest_ptr(A(0)), (char **)guest_ptr(A(1)),
                                   (int)A(2));
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               strcmp(imp->name, "abs") == 0) {
        result = (uint32_t)(A(0) & 0x80000000u ? -(int32_t)A(0) : A(0));
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               strcmp(imp->name, "_ltoa") == 0) {
        uint32_t radix = A(2);
        snprintf(guest_ptr(A(1)), 34, radix == 16 ? "%x" : radix == 8 ? "%o"
                 : "%d", (int32_t)A(0));
        result = A(1);
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               strcmp(imp->name, "_itoa_s") == 0) {
        uint32_t radix = A(3);
        snprintf(guest_ptr(A(1)), A(2), radix == 16 ? "%x" : radix == 8 ? "%o"
                 : "%d", (int32_t)A(0));
        result = 0;
    /* ---- MSVCR80 CRT: ctype ---- */
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               (strcmp(imp->name, "isspace") == 0 ||
                strcmp(imp->name, "isprint") == 0)) {
        int c0 = (int)(A(0) & 0xffu);
        result = (uint32_t)(strcmp(imp->name, "isspace") == 0
                            ? isspace(c0) != 0 : isprint(c0) != 0);
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               strcmp(imp->name, "tolower") == 0) {
        result = (uint32_t)tolower((int)(A(0) & 0xffu));
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               strcmp(imp->name, "toupper") == 0) {
        result = (uint32_t)toupper((int)(A(0) & 0xffu));
    /* ---- MSVCR80 CRT: floating point ----
     * i386 passes doubles on the stack (8 bytes each) and returns them in
     * st(0), which the lifter models. */
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               (strcmp(imp->name, "sqrt") == 0 ||
                strcmp(imp->name, "_CIsqrt") == 0)) {
        FPUSH(sqrt(MRF64(A(0))));
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               (strcmp(imp->name, "cos") == 0 ||
                strcmp(imp->name, "_CIcos") == 0)) {
        FPUSH(cos(MRF64(A(0))));
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               (strcmp(imp->name, "sin") == 0 ||
                strcmp(imp->name, "_CIsin") == 0)) {
        FPUSH(sin(MRF64(A(0))));
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               (strcmp(imp->name, "floor") == 0)) {
        FPUSH(floor(MRF64(A(0))));
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               (strcmp(imp->name, "ceil") == 0)) {
        FPUSH(ceil(MRF64(A(0))));
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               strcmp(imp->name, "_CIexp") == 0) {
        FPUSH(exp(MRF64(A(0))));
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               strcmp(imp->name, "_CIlog") == 0) {
        FPUSH(log(MRF64(A(0))));
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               strcmp(imp->name, "_CIasin") == 0) {
        FPUSH(asin(MRF64(A(0))));
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               strcmp(imp->name, "_CIacos") == 0) {
        FPUSH(acos(MRF64(A(0))));
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               strcmp(imp->name, "_CIatan2") == 0) {
        FPUSH(atan2(MRF64(A(0)), MRF64(A(2))));
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               strcmp(imp->name, "_CIpow") == 0) {
        FPUSH(pow(MRF64(A(0)), MRF64(A(2))));
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               strcmp(imp->name, "_CIfmod") == 0) {
        FPUSH(fmod(MRF64(A(0)), MRF64(A(2))));
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               strcmp(imp->name, "atof") == 0) {
        FPUSH(atof(guest_ptr(A(0))));
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               strcmp(imp->name, "_isnan") == 0) {
        double v = MRF64(A(0));
        result = v != v;
    /* ---- MSVCR80 CRT: time and randomness ---- */
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               strcmp(imp->name, "clock") == 0) {
        result = (uint32_t)nfsu2_now_ms();
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               strcmp(imp->name, "_time64") == 0) {
        int64_t t = guest_time_now();
        if (A(0)) MR64(A(0)) = (uint64_t)t;
        result = (uint32_t)t;
        result_hi = (uint32_t)((uint64_t)t >> 32);
        set_edx = 1;
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               strcmp(imp->name, "_ftime64") == 0) {
        int64_t t = guest_time_now();
        uint32_t p = A(0);
        MR64(p) = (uint64_t)t;
        MR16(p + 8u) = 0;                 /* millitm */
        MR16(p + 10u) = 0;                /* timezone */
        MR16(p + 12u) = 0;                /* dstflag */
        result = 0;
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               (strcmp(imp->name, "_localtime32") == 0 ||
                strcmp(imp->name, "_localtime64") == 0)) {
        int64_t t = strcmp(imp->name, "_localtime32") == 0
                    ? (int64_t)(int32_t)MR32(A(0)) : (int64_t)MR64(A(0));
        time_t tt = (time_t)t;
        struct tm tmv;
        if (localtime_r(&tt, &tmv)) {
            put_msvc_tm(UINT32_C(0x01128000), &tmv);
            result = UINT32_C(0x01128000);
        } else {
            result = 0;
        }
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               strcmp(imp->name, "_mktime64") == 0) {
        uint32_t p = A(0);
        struct tm tmv;
        memset(&tmv, 0, sizeof tmv);
        tmv.tm_sec = (int)MR32(p + 0u);
        tmv.tm_min = (int)MR32(p + 4u);
        tmv.tm_hour = (int)MR32(p + 8u);
        tmv.tm_mday = (int)MR32(p + 12u);
        tmv.tm_mon = (int)MR32(p + 16u);
        tmv.tm_year = (int)MR32(p + 20u);
        tmv.tm_isdst = (int)MR32(p + 32u);
        int64_t t = (int64_t)mktime(&tmv);
        result = (uint32_t)t;
        result_hi = (uint32_t)((uint64_t)t >> 32);
        set_edx = 1;
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               strcmp(imp->name, "rand") == 0) {
        result = (uint32_t)rand();
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               strcmp(imp->name, "srand") == 0) {
        srand(A(0));
        result = 0;
    /* ---- MSVCR80 CRT: printf family ---- */
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               strcmp(imp->name, "printf") == 0) {
        char buf[4096];
        int n = guest_vformat(buf, sizeof buf, guest_ptr(A(0)),
                              c->esp + 8u, 16);
        fwrite(buf, 1, (size_t)n, stderr);
        result = (uint32_t)n;
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               strcmp(imp->name, "fprintf") == 0) {
        char buf[4096];
        int n = guest_vformat(buf, sizeof buf, guest_ptr(A(1)),
                              c->esp + 12u, 16);
        fwrite(buf, 1, (size_t)n, stderr);
        result = (uint32_t)n;
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               strcmp(imp->name, "_snprintf") == 0) {
        result = (uint32_t)guest_vformat(guest_ptr(A(0)), A(1),
                                         guest_ptr(A(2)), c->esp + 16u, 16);
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               strcmp(imp->name, "_vsnprintf") == 0) {
        result = (uint32_t)guest_vformat(guest_ptr(A(0)), A(1),
                                         guest_ptr(A(2)), A(3), 64);
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               strcmp(imp->name, "sprintf_s") == 0) {
        result = (uint32_t)guest_vformat(guest_ptr(A(0)), A(1),
                                         guest_ptr(A(2)), c->esp + 16u, 16);
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               strcmp(imp->name, "vsprintf_s") == 0) {
        result = (uint32_t)guest_vformat(guest_ptr(A(0)), A(1),
                                         guest_ptr(A(2)), A(3), 64);
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               strcmp(imp->name, "_vscprintf") == 0) {
        char buf[4096];
        result = (uint32_t)guest_vformat(buf, sizeof buf, guest_ptr(A(0)),
                                         A(1), 64);
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               strcmp(imp->name, "sscanf") == 0) {
        result = 0;
    /* ---- MSVCR80 CRT: files (the browser file layer lands here later) ---- */
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               strcmp(imp->name, "__iob_func") == 0) {
        result = UINT32_C(0x01128100);
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               strcmp(imp->name, "fflush") == 0) {
        result = 0;
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               strcmp(imp->name, "fopen") == 0) {
        /* Browser file layer: try to open files from /data/ prefix.
         * The game passes guest pointers to filename (A(0)) and mode (A(1)).
         * Returns a non-zero handle on success, 0 on failure. */
        uint32_t filename_guest = A(0);
        uint32_t mode_guest = A(1);
        if (!filename_guest) { result = 0; }
        else {
            char fname[256], mode[64];
            uint32_t len = 0;
            for (len = 0; len < 255u && len < 1000u; ++len) {
                uint8_t b = MR8(filename_guest + len);
                if (!b) break;
                fname[len] = (char)b;
            }
            fname[len] = '\0';
            len = 0;
            for (len = 0; len < 63u && len < 1000u; ++len) {
                uint8_t b = MR8(mode_guest + len);
                if (!b) break;
                mode[len] = (char)b;
            }
            mode[len] = '\0';
            /* Try /data/<fname>, then just <fname> */
            const char *paths[] = { "/data/", "", NULL };
            int i = 0;
            while (paths[i]) {
                const char *path = paths[i];
                char full[512];
                if (path && *path) snprintf(full, sizeof full, "%s%s", path, fname);
                else strncpy(full, fname, sizeof full - 1);
                full[sizeof full - 1] = '\0';
                FILE *f = fopen(full, mode[0] ? mode : "rb");
                if (f) { result = 1u; fclose(f); break; }
                i++;
            }
            if (!paths[i]) result = 0;
        }
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               strcmp(imp->name, "fclose") == 0) {
        result = 0;
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               strcmp(imp->name, "fseek") == 0) {
        result = -1;
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               (strcmp(imp->name, "remove") == 0 ||
                strcmp(imp->name, "rename") == 0 ||
                strcmp(imp->name, "_chmod") == 0 ||
                strcmp(imp->name, "_mkdir") == 0)) {
        result = 0;      /* pretend: the write layer is read-only for now */
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               strcmp(imp->name, "getenv") == 0) {
        result = 0;
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               strcmp(imp->name, "_getcwd") == 0) {
        result = A(0);
        if (A(0)) guest_string(A(0), ".", A(1));
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               strcmp(imp->name, "_getdrive") == 0) {
        result = 3;      /* C: */
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               strcmp(imp->name, "_getdiskfree") == 0) {
        result = 0;
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               (strcmp(imp->name, "_findfirst64i32") == 0 ||
                strcmp(imp->name, "_findnext64i32") == 0)) {
        result = UINT32_MAX;   /* no entries */
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               strcmp(imp->name, "_findclose") == 0) {
        result = 0;
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               strcmp(imp->name, "__getmainargs") == 0) {
        /* int __getmainargs(int *argc, char ***argv, char ***envp,
         *                   int expand, _startupinfo *si) */
        uint32_t name = UINT32_C(0x01130000);
        uint32_t argv = UINT32_C(0x01131000);
        uint32_t envp = UINT32_C(0x01132000);
        guest_string(name, "nfsps.exe", 256);
        MR32(argv) = name;
        MR32(argv + 4u) = 0;
        MR32(envp) = 0;
        if (A(0)) MR32(A(0)) = 1;
        if (A(1)) MR32(A(1)) = argv;
        if (A(2)) MR32(A(2)) = envp;
        if (A(4)) memset(guest_ptr(A(4)), 0, 68);
        result = 0;
    /* ---- MSVCR80 CRT: process, threading, exceptions ---- */
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               (strcmp(imp->name, "exit") == 0 ||
                strcmp(imp->name, "_exit") == 0)) {
        g_status = 7;
        __builtin_trap();
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               strcmp(imp->name, "_beginthreadex") == 0) {
        result = 0;      /* threads arrive with the real scheduler */
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               strcmp(imp->name, "_endthreadex") == 0) {
        result = 0;
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               (strcmp(imp->name, "_onexit") == 0 ||
                strcmp(imp->name, "__dllonexit") == 0 ||
                strcmp(imp->name, "raise") == 0)) {
        result = 0;
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               (strcmp(imp->name, "_encode_pointer") == 0 ||
                strcmp(imp->name, "_decode_pointer") == 0)) {
        result = A(0);
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               (strcmp(imp->name, "_adjust_fdiv") == 0 ||
                strcmp(imp->name, "__set_app_type") == 0 ||
                strcmp(imp->name, "__CxxFrameHandler3") == 0 ||
                strcmp(imp->name, "_except_handler4_common") == 0 ||
                strcmp(imp->name, "_XcptFilter") == 0 ||
                strcmp(imp->name, "__RTDynamicCast") == 0 ||
                strcmp(imp->name, "_CxxThrowException") == 0 ||
                strcmp(imp->name, "??0exception@std@@QAE@XZ") == 0 ||
                strcmp(imp->name, "??0exception@std@@QAE@ABV01@@Z") == 0 ||
                strcmp(imp->name, "??1exception@std@@UAE@XZ") == 0)) {
        result = 0;
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               strcmp(imp->name, "?terminate@@YAXXZ") == 0) {
        g_status = 7;
        __builtin_trap();
    } else if (strcmp(imp->dll, "MSVCR80.dll") == 0 &&
               (strcmp(imp->name, "bsearch") == 0 ||
                strcmp(imp->name, "qsort") == 0)) {
        result = A(0);   /* only called once gameplay sorts something */
    } else if (strcmp(imp->dll, "d3d9.dll") == 0 &&
               strcmp(imp->name, "Direct3DCreate9") == 0) {
        result = com_direct3dcreate9();
    } else if (strcmp(imp->dll, "d3dx9_34.dll") == 0) {
        /* D3DX9 stubs: import table arity is 0 for all functions.
         * These are called during effect/shader initialization.
         * Return 0 (failure) for unimplemented functions. */
        if (strcmp(imp->name, "D3DXCreateEffectFromResourceA") == 0 ||
            strcmp(imp->name, "D3DXMatrixMultiplyTranspose") == 0 ||
            strcmp(imp->name, "D3DXCreateEffectPool") == 0 ||
            strcmp(imp->name, "D3DXSaveSurfaceToFileA") == 0 ||
            strcmp(imp->name, "D3DXGetVertexShaderProfile") == 0 ||
            strcmp(imp->name, "D3DXGetPixelShaderProfile") == 0 ||
            strcmp(imp->name, "D3DXVec3Transform") == 0 ||
            strcmp(imp->name, "D3DXVec4Transform") == 0 ||
            strcmp(imp->name, "D3DXMatrixMultiply") == 0 ||
            strcmp(imp->name, "D3DXSaveSurfaceToFileInMemory") == 0 ||
            strcmp(imp->name, "D3DXCreateCubeTextureFromFileInMemory") == 0 ||
            strcmp(imp->name, "D3DXCreateVolumeTextureFromFileInMemory") == 0 ||
            strcmp(imp->name, "D3DXMatrixTranspose") == 0 ||
            strcmp(imp->name, "D3DXMatrixInverse") == 0) {
            result = 0;
        }
    } else {
        /* No shim yet.  Record exactly which one stopped the boot and stop
         * here: letting the guest continue with a made-up return value walks
         * into a pointer the game never initialised, which is a fault we
         * cannot attribute.  The page catches RuntimeError and shows the
         * name; web/boot.mjs reads it back the same way. */
        g_unknown_imports++;
        snprintf(g_last_import, sizeof g_last_import, "%s!%s arity=%d",
                 imp->dll, imp->name, imp->nargs);
        g_status = 1;
        __builtin_trap();
    }

import_done:
    c->eax = result;
    if (set_edx)
        c->edx = result_hi;  /* 64-bit returns use edx:eax on i386 */
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

/* Diagnostics for the page and CI: which import stopped the boot, how many
 * were unsupported, and how much of the guest heap is in use. */
WASM_EXPORT("prostreet_last_import")
const char *prostreet_last_import(void)
{
    return g_last_import;
}

WASM_EXPORT("prostreet_unknown_imports")
uint32_t prostreet_unknown_imports(void)
{
    return g_unknown_imports;
}

WASM_EXPORT("prostreet_set_trace")
void prostreet_set_trace(uint32_t on)
{
    g_trace_imports = on;
}

WASM_EXPORT("prostreet_heap_used")
uint32_t prostreet_heap_used(void)
{
    return g_heap - UINT32_C(0x02000000);
}

WASM_EXPORT("prostreet_boot")
uint32_t prostreet_boot(void)
{
    g_status = 0;
    if (MR16(UINT32_C(0x00400000)) != UINT16_C(0x5a4d)) {
        g_status = 4;
        return g_status;
    }
    memset(&g_cpu, 0, sizeof g_cpu);
    g_unknown_imports = 0;
    snprintf(g_last_import, sizeof g_last_import, "none");
    g_cpu.eip = UINT32_C(0x1aaa080);
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

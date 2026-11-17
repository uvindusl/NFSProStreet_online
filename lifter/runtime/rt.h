/* ProStreet native runtime: shared declarations.
 *
 * The lifted code in out/lift-boot/ is the guest program.  This layer is the
 * Win32/MSVCR80 surface it imports: the IAT is bound to IMP_MAGIC + index and
 * dispatch_indirect() routes those addresses into win32_call().
 */
#ifndef PROSTREET_RT_H
#define PROSTREET_RT_H
#include <stdint.h>
#include "cpu.h"

typedef struct { uint32_t iat; const char *dll; const char *name; int nargs; } ImpEnt;
extern const ImpEnt g_imp[];
extern const uint32_t g_imp_count;

#define IMP_MAGIC 0xF0000000u
#define IMP_INDEX(a) ((uint32_t)((a) - IMP_MAGIC))

/* Addresses used by the boot driver.  All are above the 23.7 MB image. */
#define TEB_BASE   0x1B000000u
#define PEB_BASE   0x1B010000u
#define STACK_LO   0x7F000000u
#define STACK_HI   0x7FF00000u
#define SENTINEL   0xFFFFFFF0u

/* Guest memory (mem.c) */
void mem_init(void);
void mem_commit(uint32_t addr, uint32_t size);
int  mem_load_pe(const char *path);
int  mem_overlay_section(const char *path, const char *secname);
void mem_watch_write_range(uint32_t lo, uint32_t hi);
void mem_fault_handler_install(void);
void mem_altstack_install(void);
void mem_report(void);
extern uint8_t *g_mem;
extern uint8_t g_cbits[1u << 16];
/* The CPU the current host thread is running guest code on.  Thread-local
 * so that the fault handler reports the CPU of the thread that actually
 * faulted, not whichever thread last assigned the global. */
extern __thread CPU *g_cur_cpu;

/* dispatch.c */
void dispatch_init(void);
void dispatch_report(void);
int  guest_call(CPU *c, uint32_t fn);
void guest_backtrace(CPU *c, int maxn);
uint32_t guest_fn_from_host(const void *pc);

/* diabolus section (0x10ea000-0x1aa8000) — 874 stub targets */
extern uint32_t g_diab_count;
extern uint32_t g_diab_addr[];
extern guest_fn_t g_diab_fn[];

/* win32.c */
void win32_init(void);
void win32_report(void);
int  win32_call(CPU *c, uint32_t idx);
int  win32_trace_enabled(void);
void win32_trace_note(uint32_t idx, uint32_t ret);
/* Verify per-thread TlsAlloc/TlsGetValue/TlsSetValue isolation. */
int  win32_tls_selftest(void);
/* Set up the per-thread TLS slots and the fs:[0x2c] thread context in a TEB. */
void teb_tls_init(uint32_t teb);

/* A small log ring shared by dispatch.c / win32.c for crash reports. */
#define RTLOG_SIZE 256
typedef struct { uint32_t kind, a, b, c; } RtLog;
extern RtLog g_rtlog[RTLOG_SIZE];
extern uint64_t g_rtlog_n;
void rtlog(uint32_t kind, uint32_t a, uint32_t b, uint32_t c);
void rtlog_dump(int n);
enum { RT_IMPORT = 1, RT_INDIRECT = 2, RT_TRAP = 3, RT_FAULT = 4 };

/* Guest heap arena used by the HLE allocator (win32.c). */
uint32_t gh_alloc(uint32_t n, int zero, uint32_t align);
uint32_t gh_realloc(uint32_t p, uint32_t n);
void     gh_free(uint32_t p);
uint32_t gh_usize(uint32_t p);

/* CRT exit path: report and stop the host. */
extern int g_guest_exited;
extern int g_exit_code;
void guest_exit(int code);

#endif

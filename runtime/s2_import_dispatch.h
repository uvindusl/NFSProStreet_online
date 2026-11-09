#ifndef NFSPS_S2_IMPORT_DISPATCH_H
#define NFSPS_S2_IMPORT_DISPATCH_H

#include <stddef.h>
#include <stdint.h>

#define S2_IMP_MAGIC UINT32_C(0xF0000000)

typedef enum {
    S2_IMPORT_CDECL,
    S2_IMPORT_STDCALL
} S2ImportConvention;

typedef struct {
    const char *dll;
    const char *name;
    uint32_t arity;
    S2ImportConvention convention;
} S2ImportEntry;

typedef struct {
    uint32_t esp;
    uint32_t eax;
    uint32_t edx;
} S2ImportCpu;

typedef int (*S2ImportRead32)(void *opaque, uint32_t address, uint32_t *value);
typedef int (*S2ImportHandler)(void *opaque, const S2ImportEntry *entry,
                               const uint32_t *args, uint32_t argc,
                               uint32_t *eax, uint32_t *edx);
typedef void (*S2ImportLog)(void *opaque, const char *message);

typedef enum {
    S2_IMPORT_OK = 0,
    S2_IMPORT_NOT_MAGIC,
    S2_IMPORT_BAD_INDEX,
    S2_IMPORT_BAD_ARITY,
    S2_IMPORT_BAD_STACK,
    S2_IMPORT_UNSUPPORTED,
    S2_IMPORT_HANDLER_ERROR
} S2ImportStatus;

typedef struct {
    const S2ImportEntry *entries;
    size_t entry_count;
    S2ImportRead32 read32;
    S2ImportHandler handler;
    S2ImportLog log;
    void *opaque;
    uint32_t max_arity;
} S2ImportDispatch;

S2ImportStatus s2_import_dispatch(const S2ImportDispatch *dispatch,
                                  S2ImportCpu *cpu, uint32_t target);
const char *s2_import_status_name(S2ImportStatus status);

#endif

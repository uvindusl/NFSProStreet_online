#include "s2_import_dispatch.h"

#include <stdio.h>
#include <string.h>

static int equal_ascii(const char *a, const char *b)
{
    unsigned char ca, cb;
    if (!a || !b)
        return 0;
    while (*a || *b) {
        ca = (unsigned char)*a++;
        cb = (unsigned char)*b++;
        if (ca >= 'a' && ca <= 'z') ca -= 'a' - 'A';
        if (cb >= 'a' && cb <= 'z') cb -= 'a' - 'A';
        if (ca != cb)
            return 0;
    }
    return 1;
}

S2ImportConvention s2_import_convention(const char *dll, const char *name)
{
    if (equal_ascii(dll, "MSVCR80.dll") ||
        equal_ascii(dll, "MSVCP80.dll")) {
        /* This CRT helper is exported with stdcall despite its DLL's
         * otherwise cdecl ABI. */
        if (equal_ascii(name, "_except_handler4_common"))
            return S2_IMPORT_STDCALL;
        return S2_IMPORT_CDECL;
    }
    return S2_IMPORT_STDCALL;
}

static void report(const S2ImportDispatch *dispatch, const char *message)
{
    if (dispatch->log)
        dispatch->log(dispatch->opaque, message);
    else
        fprintf(stderr, "%s\n", message);
}

static void report_entry(const S2ImportDispatch *dispatch,
                         const char *kind, size_t index,
                         const S2ImportEntry *entry)
{
    char message[256];
    (void)snprintf(message, sizeof message,
                   "[s2-import] %s index=%zu %s!%s arity=%u",
                   kind, index, entry->dll, entry->name, entry->arity);
    report(dispatch, message);
}

const char *s2_import_status_name(S2ImportStatus status)
{
    switch (status) {
    case S2_IMPORT_OK: return "ok";
    case S2_IMPORT_NOT_MAGIC: return "not-magic";
    case S2_IMPORT_BAD_INDEX: return "bad-index";
    case S2_IMPORT_BAD_ARITY: return "bad-arity";
    case S2_IMPORT_BAD_STACK: return "bad-stack";
    case S2_IMPORT_UNSUPPORTED: return "unsupported";
    case S2_IMPORT_HANDLER_ERROR: return "handler-error";
    }
    return "unknown";
}

S2ImportStatus s2_import_dispatch(const S2ImportDispatch *dispatch,
                                  S2ImportCpu *cpu, uint32_t target)
{
    uint32_t index, args_bytes, next_esp;
    uint32_t args[S2_IMPORT_MAX_ARGS];
    const S2ImportEntry *entry;
    S2ImportStatus status = S2_IMPORT_OK;
    int handler_status;

    if (!dispatch || !cpu || !dispatch->entries || !dispatch->read32)
        return S2_IMPORT_BAD_STACK;
    if ((target & UINT32_C(0xF0000000)) != S2_IMP_MAGIC)
        return S2_IMPORT_NOT_MAGIC;

    index = target - S2_IMP_MAGIC;
    if ((size_t)index >= dispatch->entry_count) {
        report(dispatch, "[s2-import] error: import index is outside the table");
        return S2_IMPORT_BAD_INDEX;
    }
    entry = &dispatch->entries[index];
    if (entry->arity > S2_IMPORT_MAX_ARGS ||
        entry->arity > dispatch->max_arity ||
        entry->arity > (UINT32_MAX - 4u) / 4u) {
        report_entry(dispatch, "error: invalid", index, entry);
        return S2_IMPORT_BAD_ARITY;
    }

    args_bytes = entry->arity * 4u;
    if (cpu->esp > UINT32_MAX - 4u - args_bytes) {
        report_entry(dispatch, "error: stack overflow", index, entry);
        return S2_IMPORT_BAD_STACK;
    }
    if (entry->arity) {
        for (uint32_t i = 0; i < entry->arity; ++i) {
            uint32_t address = cpu->esp + 4u + i * 4u;
            if (!dispatch->read32(dispatch->opaque, address, &args[i])) {
                report_entry(dispatch, "error: unreadable argument", index, entry);
                status = S2_IMPORT_BAD_STACK;
                goto done;
            }
        }
    }

    if (!dispatch->handler) {
        report_entry(dispatch, "unsupported", index, entry);
        status = S2_IMPORT_UNSUPPORTED;
        goto done;
    }
    handler_status = dispatch->handler(dispatch->opaque, entry, args,
                                        entry->arity, &cpu->eax, &cpu->edx);
    if (handler_status) {
        report_entry(dispatch, "unsupported", index, entry);
        status = S2_IMPORT_HANDLER_ERROR;
        goto done;
    }

    next_esp = cpu->esp + 4u;
    if (entry->convention == S2_IMPORT_STDCALL)
        next_esp += args_bytes;
    cpu->esp = next_esp;

done:
    if (status != S2_IMPORT_OK)
        report(dispatch, s2_import_status_name(status));
    return status;
}

#include "s2_import_dispatch.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint32_t memory[16];
static int read32(void *opaque, uint32_t address, uint32_t *value)
{
    uint32_t *base = opaque;
    if (address < 0x1000u || address >= 0x1040u || (address & 3u))
        return 0;
    *value = base[(address - 0x1000u) / 4u];
    return 1;
}

static int add(void *opaque, const S2ImportEntry *entry, const uint32_t *args,
               uint32_t argc, uint32_t *eax, uint32_t *edx)
{
    (void)opaque;
    assert(!strcmp(entry->name, "Add"));
    assert(argc == 2);
    *eax = args[0] + args[1];
    *edx = 0;
    return 0;
}

static int unsupported(void *opaque, const S2ImportEntry *entry,
                       const uint32_t *args, uint32_t argc,
                       uint32_t *eax, uint32_t *edx)
{
    (void)opaque; (void)entry; (void)args; (void)argc; (void)eax; (void)edx;
    return 1;
}

int main(void)
{
    const S2ImportEntry entries[] = {
        {"KERNEL32.dll", "Add", 2, S2_IMPORT_STDCALL},
        {"MSVCR80.dll", "Unsupported", 1, S2_IMPORT_CDECL},
    };
    S2ImportCpu cpu = {0x1000u, 0, 0};
    S2ImportDispatch dispatch = {entries, 2, read32, add, NULL, memory, 8};

    memory[1] = 7;
    memory[2] = 5;
    assert(s2_import_dispatch(&dispatch, &cpu, S2_IMP_MAGIC) == S2_IMPORT_OK);
    assert(cpu.eax == 12 && cpu.esp == 0x100cu);

    cpu.esp = 0x1000u;
    dispatch.handler = unsupported;
    assert(s2_import_dispatch(&dispatch, &cpu, S2_IMP_MAGIC + 1) ==
           S2_IMPORT_HANDLER_ERROR);
    assert(cpu.esp == 0x1000u);

    cpu.esp = 0x103cu;
    dispatch.handler = add;
    assert(s2_import_dispatch(&dispatch, &cpu, S2_IMP_MAGIC) ==
           S2_IMPORT_BAD_STACK);
    assert(cpu.esp == 0x103cu);

    assert(s2_import_dispatch(&dispatch, &cpu, 0x1234u) ==
           S2_IMPORT_NOT_MAGIC);
    puts("s2 import dispatch: ok");
    return 0;
}

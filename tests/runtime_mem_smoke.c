#include "cpu.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    assert(argc == 2);
    mem_init();
    assert(g_mem != NULL);
    assert(mem_load_pe(argv[1]) == 0);
    assert(mem_is_committed(0x00400000));
    assert(MR16(0x00400000) == 0x5a4d);
    assert(MR32(0x0040003c) != 0);
    puts("runtime memory smoke: ok");
    return 0;
}

#include "cpu.h"

#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

void dispatch_init(void);
void import_init(void);
void sub_00828c25(CPU *);
extern volatile int g_guest_exited;
extern int g_guest_exit_code;
extern jmp_buf g_boot_jmp;
extern int g_boot_jmp_active;

int main(int argc, char **argv)
{
    assert(argc == 2);
    mem_load_pe(argv[1]);
    assert(g_mem != NULL);
    mem_commit(0x0ff00000u, 0x100000u);
    mem_commit(0x00200000u, 0x20000u);
    mem_fault_handler_install();
    MR32(0x00200018u) = 0x00210000u;
    MR32(0x00210004u) = 0x0ffffff0u;
    MR32(0x00210008u) = 0x0ff00000u;
    import_init();
    dispatch_init();

    CPU cpu;
    memset(&cpu, 0, sizeof cpu);
    cpu.esp = 0x0ffffff0u;
    cpu.top = 0;
    cpu.fcw = 0x037f;
    cpu.fs_base = 0x00200000u;
    g_cur_cpu = &cpu;
    if (setjmp(g_boot_jmp) == 0) {
        g_boot_jmp_active = 1;
        sub_00828c25(&cpu);
        g_boot_jmp_active = 0;
    } else {
        g_boot_jmp_active = 0;
    }

    printf("native boot smoke: exited=%d code=%d eip=0x%08x\n",
           g_guest_exited, g_guest_exit_code, cpu.eip);
    return g_guest_exited ? 0 : 1;
}

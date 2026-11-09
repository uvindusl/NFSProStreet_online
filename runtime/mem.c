/* Guest memory: 4 GiB reserved in wasm linear memory (base 0), or PROT_NONE
 * in the native host build.  The wasm linear memory is provided by the host
 * (wasm-ld --global-base=GUEST_END).  In both builds the guest 32-bit address
 * a maps to host address g_mem[a] (one instruction: ldr w0, [x_a]).
 *
 * DRM sections (ars, est, artem, celare, .securom) are never committed; they
 * stay PROT_NONE and a stray pointer into them faults at once instead of reading
 * zeros.  This list must stay in step with docs/BINARY_REPORT.md and
 * config/reference.json.
 */
#define _GNU_SOURCE

#include "cpu.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

/* ---------- wasm / native shared ---------- */
uint8_t *g_mem;
uint8_t g_cbits[1u << 15];
CPU *g_cur_cpu;

enum {
    MEM_PAGE_SHIFT = 14,
    MEM_PAGE_SIZE = 1u << MEM_PAGE_SHIFT,
    IMAGE_BASE = 0x00400000u,
    PE32 = 0x010b,
    PE32_PLUS = 0x020b,
    IMAGE_FILE_MACHINE_I386 = 0x014c,
    IMAGE_FILE_MACHINE_AMD64 = 0x8664,
    IMAGE_NT_SIGNATURE = 0x00004550u,
    IMAGE_NUMBEROF_DIRECTORY_ENTRIES = 16,
    IMAGE_DIRECTORY_ENTRY_BASERELOC = 5,
    SECTION_HEADER_SIZE = 40,
};

static int g_mem_reserved;
static uint8_t g_signal_stack[64u << 10];

/* The DRM sections of this release.  They are not part of the game: the
 * .text image is already decrypted and is all the lift uses.  Nothing here
 * copies, commits, reads or executes them.  Keep this list in step with
 * docs/BINARY_REPORT.md and config/reference.json. */
static const char *const g_drm_sections[] = {
    "ars", "est", "artem", "celare", ".securom", NULL
};

static int is_drm_section(const char *name)
{
    while (*name == '.')
        name++;
    for (const char *const *p = g_drm_sections; *p; p++) {
        const char *a = name, *b = *p;
        while (*a && *b) {
            char ca = (*a >= 'A' && *a <= 'Z') ? (char)(*a + 32) : *a;
            char cb = (*b >= 'A' && *b <= 'Z') ? (char)(*b + 32) : *b;
            if (ca != cb) break;
            a++; b++;
        }
        if (*a == *b && *a == '\0')
            return 1;
    }
    return 0;
}

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void fail(const char *what)
{
    fprintf(stderr, "[mem] %s: %s\n", what, strerror(errno));
}

/* ---------- wasm: reserve the wasm linear memory ---------- */
#ifdef __wasm__
void mem_init(void)
{
    if (g_mem_reserved)
        return;
    /* g_mem is already wired by the wasm host to point at the linear memory.
     * We just mark it reserved so subsequent commits are allowed. */
    g_mem_reserved = 1;
}
#else
/* Native: mmap 4 GiB PROT_NONE, then mprotect committed pages. */
void mem_init(void)
{
    if (g_mem_reserved)
        return;

    void *p = mmap(NULL, GSZ, PROT_NONE,
                  MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (p == MAP_FAILED) {
        g_mem = NULL;
        fail("unable to reserve the 4 GiB guest address space");
        return;
    }
    g_mem = p;
    memset(g_cbits, 0, sizeof(g_cbits));
    g_mem_reserved = 1;
}
#endif

/* ---------- commit guest pages ---------- */
void mem_commit(uint32_t addr, uint32_t size)
{
    static int warned;
    if (!g_mem_reserved) {
        fprintf(stderr, "[mem] mem_commit before mem_init\n");
        abort();
    }
    /* In wasm, mprotect on linear memory just makes pages RW; in native it
     * changes PROT_NONE pages to PROT_READ|PROT_WRITE. */
    uint32_t lo = addr & ~(MEM_PAGE_SIZE - 1u);
    uint64_t hi = ((uint64_t)addr + size + MEM_PAGE_SIZE - 1) & ~(uint64_t)(MEM_PAGE_SIZE - 1);
#ifdef __wasm__
    /* wasm: just record the bitmap; the memory is already RW-zero. */
    if (hi > GSZ)
        hi = GSZ;
    for (uint64_t page = lo >> MEM_PAGE_SHIFT; page < hi >> MEM_PAGE_SHIFT; ++page)
        g_cbits[page >> 3] |= (uint8_t)(1u << (page & 7));
#else
    if (hi > GSZ) {
        if (!warned++) {
            fprintf(stderr, "[mem] commit 0x%08x+%u beyond the guest address space: ignored\n",
                    addr, size);
        }
        return;
    }
    if (mprotect(g_mem + lo, (size_t)(hi - lo), PROT_READ | PROT_WRITE) != 0) {
        fail("unable to commit guest memory");
        abort();
    }
    for (uint64_t page = lo >> MEM_PAGE_SHIFT; page < hi >> MEM_PAGE_SHIFT; ++page)
        g_cbits[page >> 3] |= (uint8_t)(1u << (page & 7));
#endif
}

/* ---------- read the PE file ---------- */
static int read_file(const char *path, uint8_t **data_out, size_t *size_out)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        fail(path);
        return -1;
    }
    off_t end = lseek(fd, 0, SEEK_END);
    if (end < 0 || (uint64_t)end > SIZE_MAX) {
        fail("cannot determine executable size");
        close(fd);
        return -1;
    }
    if (lseek(fd, 0, SEEK_SET) < 0) {
        fail("cannot rewind executable");
        close(fd);
        return -1;
    }
    uint8_t *data = malloc((size_t)end);
    if (!data) {
        fail("cannot allocate executable buffer");
        close(fd);
        return -1;
    }
    size_t got = 0;
    while (got < (size_t)end) {
        ssize_t n = read(fd, data + got, (size_t)end - got);
        if (n <= 0) {
            fail("cannot read executable");
            free(data);
            close(fd);
            return -1;
        }
        got += (size_t)n;
    }
    close(fd);
    *data_out = data;
    *size_out = got;
    return 0;
}

/* ---------- load a PE32 image at 0x00400000 ---------- */
int mem_load_pe(const char *path)
{
    uint8_t *file = NULL;
    size_t file_size = 0;
    if (!path)
        return -1;
#ifdef __wasm__
    mem_init();
    if (!g_mem_reserved)
        return -1;
#else
    mem_init();
    if (!g_mem_reserved)
        return -1;
#endif
    if (read_file(path, &file, &file_size) != 0)
        return -1;

    int result = -1;
    do {
        if (file_size < 0x40 || rd16(file) != 0x5a4d) {
            fprintf(stderr, "[mem] %s: not a DOS executable\n", path);
            break;
        }
        uint32_t nt_off = rd32(file + 0x3c);
        if (nt_off > file_size || file_size - nt_off < 24) {
            fprintf(stderr, "[mem] %s: truncated PE header\n", path);
            break;
        }
        const uint8_t *nt = file + nt_off;
        if (rd32(nt) != IMAGE_NT_SIGNATURE || rd16(nt + 4) != IMAGE_FILE_MACHINE_I386) {
            fprintf(stderr, "[mem] %s: 32-bit x86 PE image required\n", path);
            break;
        }
        uint16_t sections = rd16(nt + 6);
        uint16_t optional_size = rd16(nt + 20);
        if (!sections || optional_size < 96 ||
            nt_off + 24u + optional_size + (uint32_t)sections * SECTION_HEADER_SIZE > file_size) {
            fprintf(stderr, "[mem] %s: invalid section table\n", path);
            break;
        }
        const uint8_t *opt = nt + 24;
        if (rd16(opt) != PE32) {
            fprintf(stderr, "[mem] %s: PE32 image rejected\n", path);
            break;
        }
        uint32_t preferred = rd32(opt + 28);
        if (preferred != IMAGE_BASE) {
            fprintf(stderr, "[mem] %s: preferred base 0x%08x, this port maps only 0x%08x\n",
                    path, preferred, IMAGE_BASE);
            break;
        }
        uint32_t image_size = rd32(opt + 56);
        uint32_t headers_size = rd32(opt + 60);
        uint32_t directories = rd32(opt + 92);
        if (!image_size || image_size > GSZ - IMAGE_BASE ||
            !headers_size || headers_size > image_size || headers_size > file_size ||
            directories > IMAGE_NUMBEROF_DIRECTORY_ENTRIES ||
            optional_size < 96u + directories * 8u) {
            fprintf(stderr, "[mem] %s: invalid image dimensions\n", path);
            break;
        }
        if (directories > IMAGE_DIRECTORY_ENTRY_BASERELOC &&
            rd32(opt + 96 + IMAGE_DIRECTORY_ENTRY_BASERELOC * 8) != 0) {
            fprintf(stderr, "[mem] %s: base relocations are not supported\n", path);
            break;
        }

        /* In wasm the image is copied into the wasm linear memory at IMAGE_BASE.
         * In native it is copied into the mmap'd g_mem at IMAGE_BASE. */
        mem_commit(IMAGE_BASE, headers_size);
        memcpy(g_mem + IMAGE_BASE, file, headers_size);

        const uint8_t *section = opt + optional_size;
        unsigned loaded = 0, skipped = 0;
        for (uint16_t i = 0; i < sections; ++i, section += SECTION_HEADER_SIZE) {
            char name[9];
            memset(name, 0, sizeof name);
            memcpy(name, section, 8);
            uint32_t vsize = rd32(section + 8);
            uint32_t va = rd32(section + 12);
            uint32_t raw_size = rd32(section + 16);
            uint32_t raw = rd32(section + 20);
            uint32_t span = vsize > raw_size ? vsize : raw_size;

            if (is_drm_section(name)) {
                fprintf(stderr, "[mem]   skipping DRM section %-8s rva 0x%06x (%u bytes, not mapped)\n",
                        name, va, span);
                skipped++;
                continue;
            }
            if (!span)
                continue;
            if ((uint64_t)va + span > image_size ||
                (uint64_t)raw + raw_size > file_size) {
                fprintf(stderr, "[mem] %s: section %s is outside the image\n", path, name);
                result = -1;
                goto done;
            }
            mem_commit(IMAGE_BASE + va, span);
            memset(g_mem + IMAGE_BASE + va, 0, span);
            if (raw_size)
                memcpy(g_mem + IMAGE_BASE + va, file + raw, raw_size);
            loaded++;
            fprintf(stderr, "[mem]   loaded %-8s -> 0x%08x  %u bytes\n",
                    name[0] ? name : "<unnamed>", IMAGE_BASE + va, raw_size);
        }
        result = 0;
        fprintf(stderr, "[mem] loaded %s: base 0x%08x, image %u bytes, %u sections "
                        "committed, %u DRM sections skipped\n",
                path, IMAGE_BASE, image_size, loaded, skipped);
    } while (0);
done:
    free(file);
    return result;
}

/* ---------- fault handler ---------- */
static void mem_fault(int sig, siginfo_t *info, void *context)
{
    (void)context;
    char buf[512];
    int n = 0;
    const uint8_t *p = (const uint8_t *)info->si_addr;
    if (g_mem && p >= g_mem && p < g_mem + GSZ)
        n = snprintf(buf, sizeof buf,
                     "[mem] guest memory fault: signal %d at guest 0x%08x (unmapped)\n",
                     sig, (uint32_t)(uintptr_t)(p - g_mem));
    else
        n = snprintf(buf, sizeof buf,
                     "[mem] guest memory fault: signal %d at host %p (outside the guest arena)\n",
                     sig, info->si_addr);
    if (n > 0)
        (void)write(STDERR_FILENO, buf, (size_t)n);
    if (g_cur_cpu) {
        n = snprintf(buf, sizeof buf,
                     "[mem] eax=%08x ecx=%08x edx=%08x ebx=%08x esp=%08x ebp=%08x esi=%08x edi=%08x eip=%08x fs=%08x\n",
                     g_cur_cpu->eax, g_cur_cpu->ecx, g_cur_cpu->edx, g_cur_cpu->ebx,
                     g_cur_cpu->esp, g_cur_cpu->ebp, g_cur_cpu->esi, g_cur_cpu->edi,
                     g_cur_cpu->eip, g_cur_cpu->fs_base);
        if (n > 0)
            (void)write(STDERR_FILENO, buf, (size_t)n);
    }
    _exit(128 + sig);
}

/* ---------- alternate signal stack ---------- */
void mem_altstack_install(void)
{
    stack_t stack = {
        .ss_sp = g_signal_stack,
        .ss_size = sizeof(g_signal_stack),
        .ss_flags = 0,
    };
    if (sigaltstack(&stack, NULL) != 0)
        fail("unable to install signal alternate stack");
}

/* ---------- fault handler installation ---------- */
void mem_fault_handler_install(void)
{
    struct sigaction action;
    memset(&action, 0, sizeof action);
    action.sa_sigaction = mem_fault;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&action.sa_mask);
    mem_altstack_install();
    if (sigaction(SIGSEGV, &action, NULL) != 0 ||
        sigaction(SIGBUS, &action, NULL) != 0)
        fail("unable to install guest memory fault handler");
}
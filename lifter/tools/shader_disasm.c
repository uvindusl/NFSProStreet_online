/* D3D9 Shader Bytecode Disassembler
 *
 * Reads a .bin file containing D3D9 shader bytecode and prints a human-readable
 * disassembly with version, instruction count, and per-instruction details.
 *
 * Reuses the parser from web/runtime/web/runtime/vsemu.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#define VSEMU_WGSL
#include "vsemu.h"

static uint32_t *read_shader_file(const char *path, size_t *out_dwords) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Error: cannot open '%s'\n", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 4) {
        fprintf(stderr, "Error: file too small (%ld bytes)\n", size);
        fclose(f);
        return NULL;
    }
    *out_dwords = (size_t)size / 4;
    uint32_t *tok = malloc(size);
    if (!tok) {
        fprintf(stderr, "Error: out of memory\n");
        fclose(f);
        return NULL;
    }
    size_t n = fread(tok, 1, size, f);
    fclose(f);
    if (n != (size_t)size) {
        fprintf(stderr, "Error: short read (%zu of %ld bytes)\n", n, size);
        free(tok);
        return NULL;
    }
    return tok;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <shader.bin>\n", argv[0]);
        return 1;
    }
    
    size_t max_dw = 0;
    uint32_t *tok = read_shader_file(argv[1], &max_dw);
    if (!tok) return 1;
    
    /* Read version from first token to determine vs/ps */
    uint32_t ver_token = max_dw > 0 ? tok[0] : 0;
    int is_ps = (ver_token >> 16) == 0xFFFFu;
    unsigned version = ver_token & 0xFFFFu;
    
    VSProg *p = vs_compile(tok, max_dw);
    free(tok);
    if (!p) {
        fprintf(stderr, "Error: failed to compile shader (invalid bytecode?)\n");
        return 1;
    }
    
    printf("Shader: %s\n", argv[1]);
    printf("Version: ");
    unsigned major = version >> 8;
    unsigned minor = version & 0xFF;
    if (is_ps) {
        printf("ps_%u_%u\n", major, minor);
    } else {
        printf("vs_%u_%u\n", major, minor);
    }
    printf("Instructions: %u\n", vs_ninstr(p));
    if (vs_unknown(p)) printf("Unknown tokens: %u\n", vs_unknown(p));
    printf("\n");
    
    vs_disasm(p, stdout);
    
    vs_free(p);
    return 0;
}
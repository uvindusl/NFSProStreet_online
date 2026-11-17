/*
 * msl_nrm_test.c - driver to extract NRM emissions from vs_to_msl
 * Builds with: gcc -std=c11 -O2 -DVSEMU_MSL -I... -o msl_nrm_test msl_nrm_test.c vsemu_msl.o -lm
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

/* Include the reference parser/MSL emitter header (found via -I web/runtime/web/runtime) */
#include "vsemu.h"

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <shader.bin>\n", argv[0]);
        return 1;
    }
    const char *infile = argv[1];

    /* Load blob */
    FILE *f = fopen(infile, "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", infile); return 1; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint32_t *blob = calloc((size_t)sz / 4 + 1, 4);
    if (!blob) { fclose(f); return 1; }
    size_t ndw = fread(blob, 4, (size_t)sz / 4, f);
    fclose(f);

    /* Verify version marker */
    uint32_t ver = blob[0];
    if ((ver >> 16) != 0xFFFEu) {
        free(blob);
        return 0;  /* Not a vertex shader, skip silently */
    }

    /* Compile */
    VSProg *p = vs_compile(blob, ndw);
    free(blob);
    if (!p) return 0;  /* Skip silently */

    /* Build a minimal declaration array (all unused) */
    int decl[16][2];
    for (int i = 0; i < 16; i++) { decl[i][0] = -1; decl[i][1] = 0; }

    /* Allocate output buffer */
    size_t cap = 256 * 1024;
    char *out = malloc(cap);
    if (!out) { vs_free(p); return 1; }

    /* Emit MSL */
    int ok = vs_to_msl(p, 0, decl, out, cap);
    vs_free(p);

    if (ok) {
        printf("%s", out);
    }
    free(out);
    return ok ? 0 : 1;
}
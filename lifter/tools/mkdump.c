#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wgsl.h"

static void write_file(const char *path, const char *text) {
    FILE *f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "cannot open %s for writing\n", path);
        exit(1);
    }
    fputs(text, f);
    fclose(f);
    printf("wrote %s (%zu bytes)\n", path, strlen(text));
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: mkdump <outdir>\n");
        return 1;
    }
    char path[4096];
    size_t cap = 1u << 20;
    char *buf = malloc(cap);
    if (!buf) { fprintf(stderr, "alloc failed\n"); return 1; }

    /* vs_0.wgsl */
    int n = wgsl_tvert_module(buf, cap);
    if (n <= 0) { fprintf(stderr, "wgsl_tvert_module failed: %d\n", n); return 1; }
    snprintf(path, sizeof path, "%s/vs_0.wgsl", argv[1]);
    write_file(path, buf);

    /* ps_0.wgsl */
    n = wgsl_ff_module(0, buf, cap, 0u);
    if (n <= 0) { fprintf(stderr, "wgsl_ff_module(0,0) failed: %d\n", n); return 1; }
    snprintf(path, sizeof path, "%s/ps_0.wgsl", argv[1]);
    write_file(path, buf);

    /* pulltest.wgsl */
    n = wgsl_pull_test_module(buf, cap);
    if (n <= 0) { fprintf(stderr, "wgsl_pull_test_module failed: %d\n", n); return 1; }
    snprintf(path, sizeof path, "%s/pulltest.wgsl", argv[1]);
    write_file(path, buf);

    free(buf);
    return 0;
}

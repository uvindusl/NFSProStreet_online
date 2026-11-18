/*
 * vsrun.c - native CLI for vsemu CPU interpreter.
 * Loads a vs_*.bin blob, runs vs_run on a deterministic vertex/constant set,
 * prints one line per vertex with 9 significant digits: x y z w
 *
 * Build (self-contained; no -I needed):
 *   gcc -O2 -o vsrun lifter/tools/vsrun.c -lm
 * Run:
 *   ./vsrun /tmp/opencode/ps-shaders/vs_1_1_001.bin
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

/* Include the reference parser/interpreter */
#include "../../web/runtime/web/runtime/vsemu.c"

/* Deterministic test data */
static const float TEST_VERTICES[8][16][4] = {
    /* Vertex 0: simple position with w=1, some varying attributes */
    {
        { 1.0f,  2.0f,  3.0f,  1.0f },  /* v0: POSITION */
        { 0.1f,  0.2f,  0.3f,  0.4f },  /* v1: BLENDWEIGHT */
        { 0.0f,  1.0f,  2.0f,  3.0f },  /* v2: BLENDINDICES */
        { 0.577f, 0.577f, 0.577f, 0.0f }, /* v3: NORMAL */
        { 0.1f,  0.0f,  0.0f,  1.0f },  /* v4: PSIZE */
        { 0.25f, 0.75f,  0.0f,  1.0f },  /* v5: TEXCOORD0 */
        { 0.1f,  0.2f,  0.3f,  0.4f },  /* v6: TEXCOORD1 */
        { 0.4f,  0.3f,  0.2f,  0.1f },  /* v7: TEXCOORD2 */
        { 0.1f,  0.1f,  0.1f,  0.1f },  /* v8: TEXCOORD3 */
        { 0.7f,  0.8f,  0.9f,  1.0f },  /* v9: TEXCOORD4 */
        { 0.2f,  0.4f,  0.6f,  0.8f },  /* v10: TEXCOORD5 */
        { 0.3f,  0.6f,  0.9f,  0.2f },  /* v11: TEXCOORD6 */
        { 0.5f,  0.1f,  0.5f,  0.5f },  /* v12: TEXCOORD7 */
        { 0.0f,  0.0f,  0.0f,  1.0f },  /* v13: TANGENT */
        { 0.0f,  0.0f,  0.0f,  1.0f },  /* v14: BINORMAL */
        { 0.0f,  0.0f,  0.0f,  1.0f },  /* v15: unused */
    },
    /* Vertex 1 */
    {
        { -1.0f, 2.0f, -3.0f, 2.0f },  /* v0 w=2 */
        { 0.4f,  0.3f,  0.2f,  0.1f },
        { 3.0f,  2.0f,  1.0f,  0.0f },
        { -0.577f, -0.577f, -0.577f, 0.0f },
        { 0.2f,  0.0f,  0.0f,  1.0f },
        { 0.75f, 0.25f,  0.0f,  1.0f },
        { 0.4f,  0.3f,  0.2f,  0.1f },
        { 0.1f,  0.2f,  0.3f,  0.4f },
        { 0.9f,  0.9f,  0.9f,  0.9f },
        { 0.8f,  0.7f,  0.6f,  1.0f },
        { 0.8f,  0.6f,  0.4f,  0.2f },
        { 0.9f,  0.3f,  0.6f,  0.7f },
        { 0.5f,  0.5f,  0.1f,  0.5f },
        { 0.0f,  0.0f,  0.0f,  1.0f },
        { 0.0f,  0.0f,  0.0f,  1.0f },
        { 0.0f,  0.0f,  0.0f,  1.0f },
    },
    /* Vertex 2 */
    {
        { 0.0f,  0.0f,  0.0f,  0.5f },  /* v0 w=0.5 */
        { 0.25f, 0.25f, 0.25f, 0.25f },
        { 1.0f,  0.0f,  2.0f,  1.0f },
        { 0.0f,  0.0f,  1.0f,  0.0f },
        { 0.3f,  0.0f,  0.0f,  1.0f },
        { 0.5f,  0.5f,  0.0f,  1.0f },
        { 0.0f,  0.5f,  0.5f,  0.5f },
        { 0.5f,  0.5f,  0.5f,  0.5f },
        { 0.5f,  0.5f,  0.5f,  0.5f },
        { 0.5f,  0.5f,  0.5f,  1.0f },
        { 0.5f,  0.5f,  0.5f,  0.5f },
        { 0.5f,  0.5f,  0.5f,  0.5f },
        { 0.5f,  0.5f,  0.5f,  0.5f },
        { 0.0f,  0.0f,  0.0f,  1.0f },
        { 0.0f,  0.0f,  0.0f,  1.0f },
        { 0.0f,  0.0f,  0.0f,  1.0f },
    },
    /* Vertex 3 */
    {
        { 10.0f, -10.0f, 5.0f, 1.0f },
        { 0.0f,  0.0f,  0.0f,  0.0f },
        { 0.0f,  0.0f,  0.0f,  0.0f },
        { 0.0f,  1.0f,  0.0f,  0.0f },
        { 0.0f,  0.0f,  0.0f,  1.0f },
        { 1.0f,  0.0f,  0.0f,  1.0f },
        { 0.0f,  1.0f,  0.0f,  1.0f },
        { 0.0f,  0.0f,  1.0f,  1.0f },
        { 1.0f,  1.0f,  0.0f,  1.0f },
        { 0.0f,  1.0f,  1.0f,  1.0f },
        { 1.0f,  0.0f,  1.0f,  1.0f },
        { 1.0f,  1.0f,  1.0f,  1.0f },
        { 0.5f,  0.5f,  0.5f,  0.5f },
        { 0.0f,  0.0f,  0.0f,  1.0f },
        { 0.0f,  0.0f,  0.0f,  1.0f },
        { 0.0f,  0.0f,  0.0f,  1.0f },
    },
    /* Vertex 4: larger coords */
    {
        { 100.0f, -200.0f, 300.0f, 1.0f },
        { 0.1f,  0.2f,  0.3f,  0.4f },
        { 0.0f,  1.0f,  2.0f,  3.0f },
        { 0.1f,  0.2f,  0.3f,  0.0f },
        { 0.4f,  0.0f,  0.0f,  1.0f },
        { 0.9f,  0.1f,  0.0f,  1.0f },
        { 0.8f,  0.7f,  0.6f,  0.5f },
        { 0.4f,  0.3f,  0.2f,  0.1f },
        { 0.1f,  0.1f,  0.1f,  0.1f },
        { 0.7f,  0.8f,  0.9f,  1.0f },
        { 0.2f,  0.4f,  0.6f,  0.8f },
        { 0.3f,  0.6f,  0.9f,  0.2f },
        { 0.5f,  0.1f,  0.5f,  0.5f },
        { 0.0f,  0.0f,  0.0f,  1.0f },
        { 0.0f,  0.0f,  0.0f,  1.0f },
        { 0.0f,  0.0f,  0.0f,  1.0f },
    },
    /* Vertex 5: small coords */
    {
        { 0.001f, -0.002f, 0.003f, 1.0f },
        { 0.0f,  0.0f,  0.0f,  0.0f },
        { 0.0f,  0.0f,  0.0f,  0.0f },
        { 0.0f,  0.0f,  1.0f,  0.0f },
        { 0.0f,  0.0f,  0.0f,  1.0f },
        { 0.0f,  0.0f,  0.0f,  1.0f },
        { 0.0f,  0.0f,  0.0f,  1.0f },
        { 0.0f,  0.0f,  0.0f,  1.0f },
        { 0.0f,  0.0f,  0.0f,  1.0f },
        { 0.0f,  0.0f,  0.0f,  1.0f },
        { 0.0f,  0.0f,  0.0f,  1.0f },
        { 0.0f,  0.0f,  0.0f,  1.0f },
        { 0.0f,  0.0f,  0.0f,  1.0f },
        { 0.0f,  0.0f,  0.0f,  1.0f },
        { 0.0f,  0.0f,  0.0f,  1.0f },
        { 0.0f,  0.0f,  0.0f,  1.0f },
    },
    /* Vertex 6: negative coords */
    {
        { -5.0f, -5.0f, -5.0f, 1.0f },
        { 1.0f,  0.0f,  0.0f,  0.0f },
        { 0.0f,  1.0f,  0.0f,  0.0f },
        { -0.577f, 0.577f, -0.577f, 0.0f },
        { 0.5f,  0.0f,  0.0f,  1.0f },
        { 0.0f,  1.0f,  0.0f,  1.0f },
        { 0.0f,  0.0f,  1.0f,  1.0f },
        { 1.0f,  1.0f,  0.0f,  1.0f },
        { 1.0f,  0.0f,  1.0f,  1.0f },
        { 0.0f,  1.0f,  1.0f,  1.0f },
        { 1.0f,  1.0f,  1.0f,  1.0f },
        { 0.5f,  0.5f,  0.5f,  0.5f },
        { 0.0f,  0.0f,  0.0f,  1.0f },
        { 0.0f,  0.0f,  0.0f,  1.0f },
        { 0.0f,  0.0f,  0.0f,  1.0f },
        { 0.0f,  0.0f,  0.0f,  1.0f },
    },
    /* Vertex 7: mixed w values */
    {
        { 2.0f, 3.0f, 4.0f, 3.0f },
        { 0.1f, 0.2f, 0.3f, 0.4f },
        { 1.0f, 2.0f, 3.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        { 0.6f, 0.0f, 0.0f, 1.0f },
        { 0.3f, 0.4f, 0.5f, 1.0f },
        { 0.1f, 0.2f, 0.3f, 0.4f },
        { 0.4f, 0.3f, 0.2f, 0.1f },
        { 0.1f, 0.1f, 0.1f, 0.1f },
        { 0.7f, 0.8f, 0.9f, 1.0f },
        { 0.2f, 0.4f, 0.6f, 0.8f },
        { 0.3f, 0.6f, 0.9f, 0.2f },
        { 0.5f, 0.1f, 0.5f, 0.5f },
        { 0.0f, 0.0f, 0.0f, 1.0f },
        { 0.0f, 0.0f, 0.0f, 1.0f },
        { 0.0f, 0.0f, 0.0f, 1.0f },
    }
};

/* Deterministic constants - 256 registers, 4 components each */
static VSConst make_test_consts(void) {
    VSConst k;
    memset(&k, 0, sizeof k);
    /* c0-c3: world-view-projection matrix rows (identity-ish) */
    k.f[0][0] = 1.0f; k.f[0][1] = 0.0f; k.f[0][2] = 0.0f; k.f[0][3] = 0.0f;
    k.f[1][0] = 0.0f; k.f[1][1] = 1.0f; k.f[1][2] = 0.0f; k.f[1][3] = 0.0f;
    k.f[2][0] = 0.0f; k.f[2][1] = 0.0f; k.f[2][2] = 1.0f; k.f[2][3] = 0.0f;
    k.f[3][0] = 0.0f; k.f[3][1] = 0.0f; k.f[3][2] = 0.0f; k.f[3][3] = 1.0f;
    /* c4-c7: inverse transpose */
    k.f[4][0] = 1.0f; k.f[4][1] = 0.0f; k.f[4][2] = 0.0f; k.f[4][3] = 0.0f;
    k.f[5][0] = 0.0f; k.f[5][1] = 1.0f; k.f[5][2] = 0.0f; k.f[5][3] = 0.0f;
    k.f[6][0] = 0.0f; k.f[6][1] = 0.0f; k.f[6][2] = 1.0f; k.f[6][3] = 0.0f;
    k.f[7][0] = 0.0f; k.f[7][1] = 0.0f; k.f[7][2] = 0.0f; k.f[7][3] = 1.0f;
    /* c8-c11: world matrix */
    k.f[8][0] = 2.0f; k.f[8][1] = 0.0f; k.f[8][2] = 0.0f; k.f[8][3] = 0.0f;
    k.f[9][0] = 0.0f; k.f[9][1] = 2.0f; k.f[9][2] = 0.0f; k.f[9][3] = 0.0f;
    k.f[10][0] = 0.0f; k.f[10][1] = 0.0f; k.f[10][2] = 2.0f; k.f[10][3] = 0.0f;
    k.f[11][0] = 0.0f; k.f[11][1] = 0.0f; k.f[11][2] = 0.0f; k.f[11][3] = 1.0f;
    /* c12-c15: light position/direction */
    k.f[12][0] = 1.0f; k.f[12][1] = 2.0f; k.f[12][2] = 3.0f; k.f[12][3] = 1.0f;
    k.f[13][0] = 0.5f; k.f[13][1] = 0.5f; k.f[13][2] = 0.5f; k.f[13][3] = 0.0f;
    k.f[14][0] = 1.0f; k.f[14][1] = 1.0f; k.f[14][2] = 1.0f; k.f[14][3] = 1.0f;
    k.f[15][0] = 0.25f; k.f[15][1] = 0.25f; k.f[15][2] = 0.25f; k.f[15][3] = 1.0f;
    /* c20-c23: fog params */
    k.f[20][0] = 0.0f; k.f[20][1] = 100.0f; k.f[20][2] = 1.0f; k.f[20][3] = 0.0f;
    k.f[21][0] = 1.0f; k.f[21][1] = 1.0f; k.f[21][2] = 1.0f; k.f[21][3] = 1.0f;
    /* c120: used by vs_2_0_134 / vs_3_0_051 for NRM w test */
    k.f[120][0] = 2.0f; k.f[120][1] = 2.0f; k.f[120][2] = 2.0f; k.f[120][3] = 2.0f;
    /* c121: another test constant */
    k.f[121][0] = 0.5f; k.f[121][1] = 1.5f; k.f[121][2] = 2.5f; k.f[121][3] = 3.5f;
    /* c200-c203: more constants for M4x4 etc */
    k.f[200][0] = 1.0f; k.f[200][1] = 0.0f; k.f[200][2] = 0.0f; k.f[200][3] = 0.0f;
    k.f[201][0] = 0.0f; k.f[201][1] = 1.0f; k.f[201][2] = 0.0f; k.f[201][3] = 0.0f;
    k.f[202][0] = 0.0f; k.f[202][1] = 0.0f; k.f[202][2] = 1.0f; k.f[202][3] = 0.0f;
    k.f[203][0] = 0.0f; k.f[203][1] = 0.0f; k.f[203][2] = 0.0f; k.f[203][3] = 1.0f;
    /* Integer constants */
    k.i[0][0] = 1; k.i[0][1] = 2; k.i[0][2] = 3; k.i[0][3] = 4;
    k.i[1][0] = 5; k.i[1][1] = 6; k.i[1][2] = 7; k.i[1][3] = 8;
    return k;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "usage: vsrun <shader.bin>\n");
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
        fprintf(stderr, "not a vertex shader (ver=0x%08x)\n", ver);
        free(blob);
        return 1;
    }

    /* Compile */
    VSProg *p = vs_compile(blob, ndw);
    if (!p) { fprintf(stderr, "vs_compile failed\n"); free(blob); return 1; }

    /* Run on each test vertex */
    VSConst k = make_test_consts();
    VSOut out;
    for (int vi = 0; vi < 8; vi++) {
        int ok = vs_run(p, &k, TEST_VERTICES[vi], &out);
        if (!ok) {
            fprintf(stderr, "vs_run failed on vertex %d\n", vi);
            vs_free(p);
            free(blob);
            return 1;
        }
        /* Print oPos.x oPos.y oPos.z oPos.w with 9 significant digits */
        printf("%.9g %.9g %.9g %.9g\n", out.pos[0], out.pos[1], out.pos[2], out.pos[3]);
    }

    vs_free(p);
    free(blob);
    return 0;
}
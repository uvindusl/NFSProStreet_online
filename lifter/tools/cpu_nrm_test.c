/*
 * cpu_nrm_test.c - prove vs_run's vertex NRM writes dest.w = src0.w * (1/|xyz|).
 *
 * The corpus cannot observe this: the only two vertex shaders with an unmasked
 * `nrm` (vs_2_0_134, vs_3_0_051) both contain `def c120 = 0 4 1 0`, which
 * overrides the caller's c120, so their nrm source w is 0 and r1.w is 0 either
 * way.  This driver therefore builds a synthetic vs_2_0 shader
 *
 *     dcl_position v0 ; mov r0, c0 ; nrm r1, r0 ; mov oT0, r1 ; end
 *
 * runs it with c0 = (3, 4, 0, 2) and checks oT0.  len((3,4,0)) = 5, so
 * oT0.xyz = (0.6, 0.8, 0) and oT0.w = src0.w * (1/len) = 2 * 0.2 = 0.4.
 * A regression that drops the src0.w factor makes oT0.w 1.0 instead.
 *
 * Build (from the checkout root):
 *   gcc -O2 -I web/runtime -I web/runtime/web -I web/runtime/web/runtime \
 *       -o out/tools/cpu_nrm_test lifter/tools/cpu_nrm_test.c \
 *       web/runtime/web/runtime/vsemu.c -lm
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "vsemu.h"

/* vs_2_0 ; dcl_position v0 ; mov r0, c0 ; nrm r1, r0 ; mov oT0, r1 ; end */
static const uint32_t blob[] = {
    0xFFFE0200u,
    0x0200001Fu, 0x80000000u, 0x900F0000u,   /* dcl_position v0 */
    0x02000001u, 0x800F0000u, 0xA0E40000u,   /* mov r0, c0 */
    0x02000024u, 0x800F0001u, 0x80E40000u,   /* nrm r1, r0 */
    0x02000001u, 0xE00F0000u, 0x80E40001u,   /* mov oT0, r1 */
    0x0000FFFFu,                              /* end */
};

int main(void)
{
    VSProg *p = vs_compile(blob, sizeof blob / sizeof blob[0]);
    if (!p) { fprintf(stderr, "cpu_nrm_test: vs_compile failed\n"); return 1; }

    VSConst k; memset(&k, 0, sizeof k);
    k.f[0][0] = 3.0f; k.f[0][1] = 4.0f; k.f[0][2] = 0.0f; k.f[0][3] = 2.0f;

    static const float v[16][4] = {{0}};
    VSOut o; memset(&o, 0, sizeof o);
    int ok = vs_run(p, &k, v, &o);
    vs_free(p);
    if (!ok) { fprintf(stderr, "cpu_nrm_test: vs_run failed\n"); return 1; }

    printf("oT0 = (%.9g, %.9g, %.9g, %.9g)\n",
           o.tex[0][0], o.tex[0][1], o.tex[0][2], o.tex[0][3]);

    if (fabsf(o.tex[0][0] - 0.6f) > 1e-4f || fabsf(o.tex[0][1] - 0.8f) > 1e-4f ||
        fabsf(o.tex[0][2]) > 1e-4f || fabsf(o.tex[0][3] - 0.4f) > 1e-4f) {
        fprintf(stderr, "cpu_nrm_test: FAIL (want oT0 = 0.6 0.8 0 0.4)\n");
        return 1;
    }
    printf("cpu_nrm_test: OK (oT0.w = 0.4 = src0.w * 1/|xyz|)\n");
    return 0;
}

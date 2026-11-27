/*
 * cpu_shader_semantics_test.c - prove vs_run's CPU interpreter follows D3D9
 * `cmp` and `lrp` semantics.
 *
 * These are easy to regress because the MSL/WGSL emitters and the CPU
 * interpreter implement the same opcodes separately.  debug/shader-emitter-
 * semantics.md pins the expected formulas; this driver builds a synthetic
 * vs_2_0 shader
 *
 *     dcl_position v0 ; mov r0, c0 ;
 *     cmp r1, r0, c1, c2 ; lrp r2, r0, c1, c2 ;
 *     cmp r4, c3, c1, c2 ;                  // +0, -0 and NaN sources
 *     cmp r5.x, c3.zyxw, c1, c2 ;           // source swizzle + dest mask
 *     mov oT0,r1 ; mov oT1,r2 ; mov oT2,r4 ; mov oT3,r5 ; end
 *
 * with c0 = (-1, 1, -0.5, 2), c1 = (11, 12, 13, 14),
 * c2 = (21, 22, 23, 24), c3 = (+0, -0, NaN, 3).  Therefore:
 *
 *   cmp = src0 >= 0 ? src1 : src2       -> (21, 12, 23, 14)
 *   lrp = src2 + src0 * (src1 - src2)   -> (31, 12, 28, 4)
 *   cmp boundary                        -> (11, 12, 23, 14)
 *   cmp with .zyxw and .x write mask    -> (21, 0, 0, 0)
 *
 * A missing O_CMP case leaves oT0 at zero; swapped lrp operands give
 * oT1 = (1, 22, 8, 34) instead of (31, 12, 28, 4).  A NaN output would fail
 * because check_output rejects isnan() before comparing.
 *
 * Build and run (from the checkout root):
 *   bash lifter/tools/cpu_shader_semantics_test.sh
 *
 * The wrapper creates out/tools/ and uses ${CC:-cc}.  To build by hand:
 *   gcc -O2 -I web/runtime -I web/runtime/web -I web/runtime/web/runtime \
 *       -o out/tools/cpu_shader_semantics_test \
 *       lifter/tools/cpu_shader_semantics_test.c \
 *       web/runtime/web/runtime/vsemu.c -lm
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "vsemu.h"

/* dcl_position v0 ; mov r0,c0 ; cmp r1,r0,c1,c2 ; lrp r2,r0,c1,c2 ;
 * cmp r4,c3,c1,c2 ; cmp r5.x,c3.zyxw,c1,c2 ; mov oT0..oT3 ; end */
static const uint32_t blob[] = {
    0xFFFE0200u,
    0x0200001Fu, 0x80000000u, 0x900F0000u,                    /* dcl_position v0 */
    0x02000001u, 0x800F0000u, 0xA0E40000u,                    /* mov r0, c0 */
    0x04000058u, 0x800F0001u, 0x80E40000u,
                 0xA0E40001u, 0xA0E40002u,                    /* cmp r1, r0, c1, c2 */
    0x04000012u, 0x800F0002u, 0x80E40000u,
                 0xA0E40001u, 0xA0E40002u,                    /* lrp r2, r0, c1, c2 */
    0x04000058u, 0x800F0004u, 0xA0E40003u,
                 0xA0E40001u, 0xA0E40002u,                    /* cmp r4, c3, c1, c2 */
    0x04000058u, 0x80010005u, 0xA0C60003u,
                 0xA0E40001u, 0xA0E40002u,                    /* cmp r5.x, c3.zyxw, c1, c2 */
    0x02000001u, 0xE00F0000u, 0x80E40001u,                    /* mov oT0, r1 */
    0x02000001u, 0xE00F0001u, 0x80E40002u,                    /* mov oT1, r2 */
    0x02000001u, 0xE00F0002u, 0x80E40004u,                    /* mov oT2, r4 */
    0x02000001u, 0xE00F0003u, 0x80E40005u,                    /* mov oT3, r5 */
    0x0000FFFFu,                                              /* end */
};

static int check_output(const char *name, const float got[4], const float want[4])
{
    printf("%s = (%.9g, %.9g, %.9g, %.9g)\n", name,
           got[0], got[1], got[2], got[3]);
    for (int i = 0; i < 4; i++) {
        if (isnan(got[i]) || fabsf(got[i] - want[i]) > 1e-4f) {
            fprintf(stderr, "%s: FAIL (want %.9g, %.9g, %.9g, %.9g)\n",
                    name, want[0], want[1], want[2], want[3]);
            return 0;
        }
    }
    return 1;
}

int main(void)
{
    VSProg *p = vs_compile(blob, sizeof blob / sizeof blob[0]);
    if (!p) { fprintf(stderr, "cpu_shader_semantics_test: vs_compile failed\n"); return 1; }

    VSConst k; memset(&k, 0, sizeof k);
    static const float src0[4] = {-1.f, 1.f, -0.5f, 2.f};
    static const float src1[4] = {11.f, 12.f, 13.f, 14.f};
    static const float src2[4] = {21.f, 22.f, 23.f, 24.f};
    static const float src3[4] = {0.f, -0.f, NAN, 3.f};
    for (int i = 0; i < 4; i++) {
        k.f[0][i] = src0[i]; k.f[1][i] = src1[i];
        k.f[2][i] = src2[i]; k.f[3][i] = src3[i];
    }

    static const float v[16][4] = {{0}};
    VSOut o; memset(&o, 0, sizeof o);
    int ok = vs_run(p, &k, v, &o);
    vs_free(p);
    if (!ok) { fprintf(stderr, "cpu_shader_semantics_test: vs_run failed\n"); return 1; }

    static const float want_cmp[4] = {21.f, 12.f, 23.f, 14.f};
    static const float want_lrp[4] = {31.f, 12.f, 28.f, 4.f};
    static const float want_edge[4] = {11.f, 12.f, 23.f, 14.f};
    static const float want_mask[4] = {21.f, 0.f, 0.f, 0.f};
    if (!check_output("oT0(cmp)", o.tex[0], want_cmp)) return 1;
    if (!check_output("oT1(lrp)", o.tex[1], want_lrp)) return 1;
    if (!check_output("oT2(edges)", o.tex[2], want_edge)) return 1;
    if (!check_output("oT3(mask)", o.tex[3], want_mask)) return 1;

    printf("cpu_shader_semantics_test: OK (cmp and lrp match D3D9)\n");
    return 0;
}

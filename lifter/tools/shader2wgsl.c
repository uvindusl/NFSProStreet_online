/*
 * shader2wgsl.c - offline D3D9 shader bytecode -> WGSL translator.
 *
 * ProStreet ships D3D9 tokenized shader bytecode (NOT DXBC).  This tool links
 * the reference parser/emitter (web/runtime/web/runtime/vsemu.c compiled with
 * -DVSEMU_WGSL, which pulls in web/runtime/web/vsemu_wgsl.inc) and translates a
 * raw .bin blob to a WGSL module, so real shaders can be translated offline
 * before the browser renderer exists.
 *
 * Build (native, no wasm):
 *   gcc -DVSEMU_WGSL \
 *       -I web/runtime -I web/runtime/web -I web/runtime/web/runtime \
 *       -o shader2wgsl lifter/tools/shader2wgsl.c \
 *          web/runtime/web/runtime/vsemu.c -lm
 *
 * Usage:
 *   shader2wgsl <shader.bin> [-o out.wgsl] [--test]
 *   shader2wgsl --selftest [--test]     # use the embedded synthetic blob
 *
 * --test runs the reference pixel-shader interpreter on the same program, to
 * prove the parser + interpreter path works on the synthetic blob.  It does
 * NOT interpret the emitted WGSL text; the WGSL semantics are checked against
 * the browser renderer by web/tools/wgsl-xl.c.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>

/* ---- include the reference parser ----
 * vsemu_wgsl.inc is NOT included here: it shares the parser's private types
 * and is pulled in at the end of vsemu.c under -DVSEMU_WGSL.  Compile this
 * tool together with vsemu.c (see the build command in the header comment). */
#include "wgsl.h"

/* ---- synthetic test shader (embedded) ----
 * A minimal, hand-assembled ps_2_0 blob:
 *     mov r0, v0
 *     mul r0, r0, c0
 *     mov oC0, r0
 *     end
 * D3D9 token encoding (matches the parser's macros in vsemu.c):
 *   instruction token = opcode | (operand_count << 24)
 *   source operand token = 0x80000000 | (type & 7) << 28 | (type >> 3) << 11
 *                         | 0x00E40000 (identity swizzle) | regnum
 *   dest operand token   = 0x80000000 | (type & 7) << 28 | (type >> 3) << 11
 *                         | 0x000F0000 (write mask .xyzw) | regnum
 *   types: 0=temp r#, 1=input v#, 2=const c#, 8=colorout oC#
 */
static const uint32_t test_ps_blob[] = {
    0xFFFF0200u,                                          /* ps_2_0           */
    0x02000001u, 0x800F0000u, 0x90E40000u,                /* mov r0, v0       */
    0x03000005u, 0x800F0000u, 0x80E40000u, 0xA0E40000u,   /* mul r0, r0, c0   */
    0x02000001u, 0x800F0800u, 0x80E40000u,                /* mov oC0, r0      */
    0x0000FFFFu,                                          /* end              */
};

/* ---- test sampler: every stage returns white ---- */
static void test_tex(int stage, const float *coord, int ncoord, float out[4])
{
    (void)stage; (void)coord; (void)ncoord;
    out[0] = out[1] = out[2] = out[3] = 1.0f;
}

/* ---- main ---- */
int main(int argc, char *argv[])
{
    const char *infile = NULL;
    const char *outfile = NULL;
    int do_test = 0, do_selftest = 0;

    /* Parse args */
    for (int i = 1; i < argc; i++){
        if (strcmp(argv[i], "--test") == 0) do_test = 1;
        else if (strcmp(argv[i], "--selftest") == 0) do_selftest = 1;
        else if (argv[i][0] == '-'){ /* skip unrecognized flags */ }
        else if (!infile) infile = argv[i];
        else if (!outfile) outfile = argv[i];
    }

    if (!infile && !do_selftest){
        fprintf(stderr, "usage: shader2wgsl <shader.bin> [-o out.wgsl] [--test]\n"
                        "       shader2wgsl --selftest [--test]\n");
        return 1;
    }

    /* Load the blob: from a file, or the embedded synthetic one. */
    uint32_t *blob = NULL;
    size_t ndw = 0;
    if (infile){
        FILE *f = fopen(infile, "rb");
        if (!f){ fprintf(stderr, "cannot open %s\n", infile); return 1; }
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        blob = calloc((size_t)sz / 4 + 1, 4);
        if (!blob){ fclose(f); return 1; }
        ndw = fread(blob, 4, (size_t)sz / 4, f);
        fclose(f);
    } else {
        ndw = sizeof test_ps_blob / sizeof test_ps_blob[0];
        blob = calloc(ndw + 1, 4);
        if (!blob) return 1;
        memcpy(blob, test_ps_blob, sizeof test_ps_blob);
    }

    /* Verify version marker */
    uint32_t ver = blob[0];
    if ((ver >> 16) != 0xFFFEu && (ver >> 16) != 0xFFFFu){
        fprintf(stderr, "not a D3D9 shader (ver=0x%08x)\n", ver);
        free(blob);
        return 1;
    }

    /* Compile the token stream into VSProg */
    VSProg *p = vs_compile(blob, ndw);
    if (!p){ fprintf(stderr, "vs_compile failed\n"); free(blob); return 1; }

    /* Determine whether this is a vertex or pixel shader */
    int is_ps = ((ver >> 16) == 0xFFFFu);

    /* Emit WGSL.  Real ProStreet shaders can be large (the emitter writes a
     * per-instruction body then wraps it), so size the buffer generously. */
    size_t wcap = 1u << 20;
    char *wgsl = malloc(wcap);
    if (!wgsl){ free(p); free(blob); return 1; }
    /* A standalone shader blob carries no D3DVERTEXELEMENT9 declaration, so the
     * vertex-input reads cannot be resolved offline: every v# is emitted as
     * FLOAT1 at offset 0.  This is a placeholder for naga validation only, not
     * a claim about the real draw.  The runtime passes the true declaration to
     * vs_to_wgsl (com.c gpuvs_id), where decl[i] = (byte offset, D3DDECLTYPE)
     * -- see vsemu.h:46. */
    static const int vdecl_placeholder[16][2] = {{0}};
    int w = is_ps ? ps_to_wgsl(p, 0, wgsl, wcap)
                  : vs_to_wgsl(p, 0, vdecl_placeholder, wgsl, wcap);
    if (w <= 0){ fprintf(stderr, "WGSL emission failed (rc=%d)\n", w); free(wgsl); free(p); free(blob); return 1; }

    /* ---- optional --test: run the reference interpreter on the same program ---- */
    if (do_test){
        /* A tiny shader needs only a constant buffer and vertex inputs; fill
         * them with white and run the interpreter to prove the parser and the
         * reference pixel-shader interpreter agree on this blob. */
        PSRefPar P;
        PSRefIn  in;
        memset(&P, 0, sizeof P);
        memset(&in, 0, sizeof in);
        P.aref = 0.5f;
        P.proj[0] = 1.0f;                    /* no projection divide */
        for (int i = 0; i < 4; i++){
            P.c[0][i]   = 1.0f;              /* c0 = white */
            in.col[0][i] = 1.0f;
            in.uv[0][i]  = 1.0f;
        }

        float out[4] = {0, 0, 0, 0};
        int killed = 0;
        int ok = ps_ref_run(p, &P, &in, test_tex, 0u, out, &killed);

        if (!ok || killed){
            fprintf(stderr, "--test: interpreter failed (ok=%d killed=%d)\n", ok, killed);
            free(p); free(blob); return 1;
        }

        fprintf(stderr, "--test: interpreter OK (%u instructions), "
                        "out=%.3f %.3f %.3f %.3f\n",
                vs_ninstr(p), out[0], out[1], out[2], out[3]);
    }

    /* Write output file */
    if (outfile){
        FILE *fout = fopen(outfile, "w");
        if (!fout){ fprintf(stderr, "cannot write %s\n", outfile); free(p); free(blob); return 1; }
        fputs(wgsl, fout);
        fclose(fout);
    } else {
        printf("%s", wgsl);
    }

    free(wgsl);
    free(p);
    free(blob);
    return 0;
}
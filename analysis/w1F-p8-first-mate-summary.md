Ship's scribe (w1F:pP) to First Mate (w1F:p8) — Half B: 8-wide inter-stage ABI widening
Section 5: Pixel oracle UV widened — PSRefIn.uv[4][4]→uv[8][4] (41 floats), PSRefExec.t→t[8][4], R_ADDR &3→&7, ps_ref_run tc[8][2] loop, dcl-uv &O_TEXCOORD &3→&7, texld smp &7
Section 6: Reference-vector formats — .vec row 29→45 (.vec: pos+col0+col1+uv0..uv7+fog), .pvec row 25→41 (col0+col1+uv0..uv7+fog), k-loop extended to 8 in write_ps_vectors
Section 7: Harness psDiff entry-point accepts both @location(0) vec4f and PSOut return forms; regex matches both, rewrites to fn psfn(i: VOut) → <same>; call site uses psfn(i).col for PSOut form, psfn(i) otherwise — unblocks 58 PS modules writing oDepth → PSOut
Verified: gcc compile exits 0 (pre-existing warnings only); ./out/tools/wgsl-xl --corpus → "297 vertex shaders, 294 pixel shaders, 0 failures"; vec-verify2 contains 297 .vec + 294 .pvec files
Commit: a02ff1f Half B: 8-wide inter-stage ABI widening (3 files: vsemu_psref.inc, shadercheck.ts, wgsl-xl.c)
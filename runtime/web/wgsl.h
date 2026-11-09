/* WGSL backend of the D3D9 shader translator (vsemu_wgsl.inc, compiled as
 * part of the runtime).  Same signatures and return convention as the MSL
 * emitters (length of the text, 0 = cannot translate).  Entry points:
 * vs_<id> / ps_<id>.*/
#ifndef WGSL_H
#define WGSL_H
#include <stddef.h>
#include <stdint.h>
/* Built-in modules for draws without a game shader: vs_0 (TVert pass-through)
 * and ps_<id> = col0 * tex0 (id 0, cube twin 256 with cubemask 1). */
int wgsl_tvert_module(char *out, size_t cap);
int wgsl_ff_module(int id, char *out, size_t cap, unsigned cubemask);
/* Compute module checking the vertex-pulling helpers (shadercheck test). */
int wgsl_pull_test_module(char *out, size_t cap);
#endif
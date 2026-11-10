/* WGSL shader translator for the ProStreet browser port.
 *
 * Translates D3D9 vertex/pixel shaders to WebGPU Shading Language (WGSL).
 * Built-in modules for draws without game shaders; game shaders would
 * be compiled from the guest's bytecode and handed to the page for WebGPU.
 *
 * Return values: length of the WGSL text written, or 0 = cannot translate.
 * The page's game-worker.ts maintains a registry of compiled WGSL shaders.
 */

#include "wgsl.h"
#include "cpu.h"
#include "gpu_proto.h"
#include "gfx.h"
#include <stdio.h>
#include <string.h>

/* ---- vs_0: TVert pass-through shader ---- */
int wgsl_tvert_module(char *out, size_t cap)
{
    if (!out || cap < 1)
        return 0;
    /* Simple pass-through vertex shader that just forwards attributes */
    const char *text =
        "// vs_0: TVert pass-through\n"
        "struct VsIn {\n"
        "  @location(0) pos: vec4f;\n"
        "  @location(1) tex: vec2f;\n"
        "};\n"
        "struct VsOut {\n"
        "  @builtin(position) clipPos: vec4f;\n"
        "  @location(0) tex: vec2f;\n"
        "};\n"
        "VsOut main(VsIn in) {\n"
        "  VsOut out;\n"
        "  out.clipPos = in.pos;\n"
        "  out.tex = in.tex;\n"
        "  return out;\n"
        "}\n";
    size_t n = strlen(text);
    if (n >= cap)
        return 0;
    memcpy(out, text, n);
    out[n] = '\0';
    return (int)n;
}

/* ---- ps_<id>: pixel shader modules ---- */
/* Pre-built simple pixel shaders for the null renderer.
 * ps_<id> = col0 * tex0 (id 0, cube twin 256 with cubemask 1). */

int wgsl_ff_module(int id, char *out, size_t cap, unsigned cubemask)
{
    (void)id;
    (void)cubemask;
    if (!out || cap < 1)
        return 0;
    const char *text =
        "// ps_0: simple color\n"
        "@fragment\n"
        "fn main(@location(0) tex: vec2f) -> @location(0) vec4f {\n"
        "  return vec4f(1.0, 0.0, 1.0, 1.0);\n"
        "}\n";
    size_t n = strlen(text);
    if (n >= cap)
        return 0;
    memcpy(out, text, n);
    out[n] = '\0';
    return (int)n;
}

/* ---- wgsl_pull_test_module ---- */
int wgsl_pull_test_module(char *out, size_t cap)
{
    (void)out; (void)cap;
    /* Placeholder: vertex-pulling helper test */
    return 0;
}
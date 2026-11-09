/* texrep.h -- texture replacement declarations for the WebGPU renderer.
 * Included by gpu_web.c and the page-side web/src/gpu/texrep.ts.
 *
 * The game uploads textures (DXT1/3/5, A8, etc.).  This header declares
 * the replacement hooks that the page calls into the C runtime.
 *
 * Off by default: with NFSU2_UI_TEXTURES unset every hook returns at its
 * first lines, so the game sees its original textures unchanged.
 */

#ifndef NFSPS_TEXREP_H
#define NFSPS_TEXREP_H

#include <stdint.h>

/* Called when the game uploads a texture at the given slot, size and format.
 * The runtime may replace it with an upscaled version from a texpack, or
 * keep the original.  Return non-zero if the upload was handled (texture
 * data consumed), zero if the game should keep its original data. */
int texrep_upload(unsigned id, unsigned w, unsigned h, unsigned levels,
                  unsigned fmt, const uint8_t *data);

/* Called when the game queries the pitch (bytes per row) of a texture level.
 * The runtime may return a different pitch for an upscaled texture. */
unsigned texrep_level_pitch(unsigned w, unsigned level, unsigned fmt);

/* Called when the game queries the number of rows in a texture level. */
unsigned texrep_level_rows(unsigned h, unsigned level, unsigned fmt);

/* Called when the game replaces / frees a texture. */
void texrep_free(unsigned id);

#endif
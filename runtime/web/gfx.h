/* gfx.h -- Vertex format shared by the guest (lifted C) and the renderer.
 * Positions are in clip space.  The rasterizer does the perspective divide.
 * Texture coordinates carry FOUR components per stage.
 */
#ifndef NFSPS_GFX_H
#define NFSPS_GFX_H

typedef struct {
    float x, y, z, w;      /* clip space */
    float r, g, b, a;      /* oD0 - diffuse */
    float r1, g1, b1, a1;  /* oD1 - specular */
    float t[4][4];         /* oT0..oT3, all four components */
} TVert;

#endif
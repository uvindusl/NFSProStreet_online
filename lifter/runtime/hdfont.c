/* HD font textures: glyph UVs from the original texture size.
 *
 * The game turns a font's glyph rectangles (texels) into UVs by dividing by
 * the font texture's width and height, read from the TextureInfo (int16 at
 * +0x28 and +0x2a).  HD texture packs (the "NFSPS HD Content Support" mod's
 * data) replace the font textures with larger ones but keep the original
 * glyph metrics, and the mod's HDFontSupport option hooks these reads to
 * return the original size for the known font textures.  Without it every
 * glyph samples a fragment of the HD atlas (garbled title text).
 *
 * Same table here, applied after each read.  On the original data the
 * textures already have these sizes, so this changes nothing there.
 * PROSTREET_HDFONT=0 turns it off. */
#include <stdlib.h>
#include "cpu.h"

#ifndef HOOK_SITE
#define HOOK_SITE(va) (va##u)
#endif

static const struct { uint32_t hash; uint16_t w, h; } k_fonts[] = {
    { 0x94af4029u, 256, 256 },      /* FONT_NFS_BODY */
    { 0x94b51f10u, 256, 256 },      /* FONT_NFS_MENU */
    { 0x2ab5f1c2u, 256, 256 },
    { 0xea23ae36u, 128, 128 },
    { 0x2bd9f6bdu, 512, 256 },
    { 0xd51e0499u, 1024, 512 },
};

/* 1 and sets *w, *h when the TextureInfo at ti is one of the fonts above */
static int font_size(uint32_t ti, uint32_t *w, uint32_t *h) {
    static int off = -1;
    if (off < 0) { const char *e = getenv("PROSTREET_HDFONT"); off = e && *e == '0'; }
    if (off || !ti) return 0;
    uint32_t hash = MR32(ti + 0x0c);
    for (unsigned i = 0; i < sizeof k_fonts / sizeof k_fonts[0]; i++)
        if (k_fonts[i].hash == hash) { *w = k_fonts[i].w; *h = k_fonts[i].h; return 1; }
    return 0;
}

/* Called before the instruction at ip; each site follows a width or height read. */
int hdfont_hook(CPU *c, uint32_t ip) {
    static _Thread_local uint32_t ti;   /* sub_005a1720 / sub_005a1970: edx is overwritten by the height read; per guest thread */
    uint32_t w, h;
    switch (ip) {
    case HOOK_SITE(0x00700618): if (font_size(c->esi, &w, &h)) c->edi = w; return 1;   /* edi = [esi+0x28] */
    case HOOK_SITE(0x00700620): if (font_size(c->esi, &w, &h)) c->edi = h; return 1;   /* edi = [esi+0x2a] */
    case HOOK_SITE(0x005a1734):                                                        /* edi = [edx+0x28] */
    case HOOK_SITE(0x005a1984):
        ti = c->edx;
        if (font_size(ti, &w, &h)) c->edi = w;
        return 1;
    case HOOK_SITE(0x005a175f):                                                        /* edx = [ti+0x2a] */
    case HOOK_SITE(0x005a199f):
        if (font_size(ti, &w, &h)) c->edx = h;
        return 1;
    }
    return 0;
}

#ifndef GFX_BLIT_H
#define GFX_BLIT_H

#include "hge_gfx.h"

// Low-power CPU 2D rasterizer backend. Draws the same quads as gfx_sdl but
// with integer math (and optional NEON) straight into an ARGB8888 buffer,
// avoiding SDL's per-pixel software renderer. Intended for the libretro core
// (and other low-power targets); desktop keeps using gfx_sdl.
IGfx* CreateGfxBlit();

// Framebuffer produced by the blitter (valid after Init). ARGB8888, top-down.
const void* BlitFramebuffer(int* w, int* h, int* pitch);

// Rasterizes all quads queued since the last flush (the true frame boundary).
void BlitFlush();

#endif // GFX_BLIT_H

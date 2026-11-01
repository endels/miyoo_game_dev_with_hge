#ifndef GFX_SDL_H
#define GFX_SDL_H

#include "gfx/igfx.h"

// Software-friendly SDL2 renderer backend for the IGfx seam.
// Uses only universally supported SDL_Renderer primitives
// (SDL_RenderCopyEx + color/alpha mod) so it works on the Miyoo "mmiyoo"
// and "software" renderers as well as desktop SDL2. No GL context needed.
IGfx* CreateGfxSDL();

#endif // GFX_SDL_H

#ifndef HGE_GFX_H
#define HGE_GFX_H

// Public graphics entry point. Games ask the engine for a renderer instead of
// knowing which backend (GL / SDL_Renderer / CPU blitter) the build selected.

#include "gfx/igfx.h"

namespace hge {

IGfx* CreateGfx();

} // namespace hge

#endif // HGE_GFX_H

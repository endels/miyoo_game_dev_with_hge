// Backend selection lives here, not in game code. The build system defines
// exactly one of HGE_GFX_GL / HGE_GFX_SDL / HGE_GFX_BLIT.

#include "hge_gfx.h"

#if defined(HGE_GFX_BLIT)
#include "gfx/gfx_blit.h"
#elif defined(HGE_GFX_SDL)
#include "gfx/gfx_sdl.h"
#else
#include "gfx/gfx_gl.h"
#endif

namespace hge {

IGfx* CreateGfx()
{
#if defined(HGE_GFX_BLIT)
    return CreateGfxBlit();
#elif defined(HGE_GFX_SDL)
    return CreateGfxSDL();
#else
    return CreateGfxGL();
#endif
}

} // namespace hge

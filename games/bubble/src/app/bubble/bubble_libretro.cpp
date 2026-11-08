// Bubble's only libretro-specific code: create the game and describe it.

#include "hge_libretro.h"
#include "app/bubble/bubble_app.h"

namespace hge {
namespace libretro {

IGame* CreateGame()
{
    // libretro never runs main(), so nothing would parse --mode/--select here.
    // Start on the ring mode select like the standalone build does; the core
    // maps the d-pad to aimLeft/aimRight and A to firePressed, which is exactly
    // what the menu consumes.
    bbl::BubbleOptions opt;
    opt.modeSelect = true;
    return new bbl::BubbleApp(opt);
}

const GameInfo& GetGameInfo()
{
    static const GameInfo info = {
        "Bubble Versus", "1.0",
        640, 480,     // framebuffer handed to the frontend
        960, 540,     // logical draw space
        60.0, 44100.0
    };
    return info;
}

} // namespace libretro
} // namespace hge

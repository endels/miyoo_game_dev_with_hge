// Bubble's only libretro-specific code: create the game and describe it.

#include "hge_libretro.h"
#include "app/bubble/bubble_app.h"

namespace hge {
namespace libretro {

IGame* CreateGame()
{
    return new bbl::BubbleApp(bbl::BubbleOptions{});
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

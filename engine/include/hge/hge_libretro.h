#ifndef HGE_LIBRETRO_H
#define HGE_LIBRETRO_H

// Generic libretro host interface. The engine provides the retro_* entry
// points; the game only supplies a game instance and its metadata.

#include "hge_app.h"

namespace hge {
namespace libretro {

struct GameInfo {
    const char* name;
    const char* version;
    unsigned    video_width;    // framebuffer handed to the frontend
    unsigned    video_height;
    unsigned    logical_width;  // coordinate space the game draws in
    unsigned    logical_height;
    double      fps;
    double      sample_rate;
};

// Implemented by the game.
IGame*            CreateGame();
const GameInfo&   GetGameInfo();

} // namespace libretro
} // namespace hge

#endif // HGE_LIBRETRO_H

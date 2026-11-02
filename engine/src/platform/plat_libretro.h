#ifndef BBL_PLAT_LIBRETRO_H
#define BBL_PLAT_LIBRETRO_H

// Platform layer used when the engine runs as a libretro core. The frontend
// owns video/audio/input; this layer:
//  * runs the configured engine entry on a coroutine so each retro_run()
//    can advance exactly one frame (SwapBuffers yields back to the frontend),
//  * renders with SDL's software renderer into an ARGB8888 surface that the
//    core hands to the frontend,
//  * exposes input as the edge/held button masks set by the core each frame.

enum PlatRetroBtn {
    PB_LEFT   = 1u << 0,
    PB_RIGHT  = 1u << 1,
    PB_UP     = 1u << 2,
    PB_DOWN   = 1u << 3,
    PB_A      = 1u << 4,
    PB_B      = 1u << 5,
    PB_X      = 1u << 6,
    PB_Y      = 1u << 7,
    PB_START  = 1u << 8,
    PB_SELECT = 1u << 9,
    PB_L      = 1u << 10,
    PB_R      = 1u << 11
};

// Entry point supplied by the engine's generic libretro host.
typedef int (*PlatLibretroEntry)(int argc, char** argv);
void plat_libretro_set_entry(PlatLibretroEntry fn);

// Runs the game until its first SwapBuffers (i.e. init + one frame).
// Returns 1 while the game is still alive, 0 if it exited during init.
int plat_libretro_begin();

// Advances exactly one frame. Returns 0 once the game has exited.
int plat_libretro_step();

// Current frame buffer (ARGB8888), valid after begin/step.
const void* plat_libretro_fb(int* w, int* h, int* pitch);

// Called by the core before each step. `pressed` holds buttons that went down
// since the previous frame.
void plat_libretro_set_buttons(unsigned held, unsigned pressed);

// Releases SDL resources. Must be called from retro_deinit (after the frontend
// has stopped using the last frame we handed it).
void plat_libretro_cleanup();

#endif // BBL_PLAT_LIBRETRO_H

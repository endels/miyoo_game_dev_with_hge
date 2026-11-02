#ifndef HGE_PLAT_SDL2_INTERNAL_H
#define HGE_PLAT_SDL2_INTERNAL_H

// Internal seam between the SDL2 common layer and the desktop / Miyoo
// backends. Only the selected backend .cpp is compiled, so no platform macro
// leaks into this header or into the common layer.

#include "platform/plat.h"
#include <SDL.h>

namespace plat {
namespace detail {

extern SDL_Window*   g_win;
extern SDL_GLContext g_ctx;
extern int           g_w;
extern int           g_h;
extern int           g_ww;
extern int           g_wh;
extern Uint64        g_freq;
extern Uint64        g_t0;
extern u16           g_vk2sc[256];

void buildKeyMap();

// Implemented by plat_sdl2_desktop.cpp or plat_sdl2_miyoo.cpp.
bool  backendInit(int width, int height, const char* title, bool vsync, bool resizable);
void  backendShutdown();
void  backendUpdateSize();
void* backendRenderer();
void* backendGLGetProc(const char* name);
void  backendSwapBuffers();
void  backendMessageBox(const char* title, const char* text);
bool  backendPoll(const SDL_Event& e, Event& ev);
int   backendKeyDown(int hgeKey);   // -1 = not handled, 0 = up, 1 = down
bool  backendManualFrameCap();

} // namespace detail
} // namespace plat

#endif // HGE_PLAT_SDL2_INTERNAL_H
